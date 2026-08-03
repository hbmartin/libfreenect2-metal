/*
 * Offline camera calibration for libfreenect2 recordings.
 *
 * Licensed under either Apache-2.0 or GPL-2.0. See the project root.
 */

#include <libfreenect2/calibration_profile.h>
#include <libfreenect2/frame_listener_impl.h>
#include <libfreenect2/libfreenect2.hpp>
#include <libfreenect2/packet_pipeline.h>

#include <nlohmann/json.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#if CV_VERSION_MAJOR >= 5
#include <opencv2/objdetect.hpp>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace lf = libfreenect2;
using Json = nlohmann::json;

namespace
{

struct QualityGates
{
  uint32_t minimum_views = 20;
  double maximum_intrinsic_rms_px = 1.0;
  double maximum_stereo_rms_px = 1.5;
  double maximum_depth_rmse_mm = 20.0;
};

struct Job
{
  fs::path path;
  std::vector<fs::path> color;
  std::vector<fs::path> ir;
  std::vector<fs::path> stereo;
  std::vector<fs::path> depth;
  int columns = 0;
  int rows = 0;
  double square_size_mm = 0.0;
  lf::DistortionModel distortion_model = lf::DistortionModel::BrownConrady5;
  QualityGates gates;
};

struct Arguments
{
  std::string command;
  std::map<std::string, std::string> values;
  bool allow_low_quality = false;
  bool allow_serial_mismatch = false;
};

bool fail(const std::string& message, std::string* error)
{
  if (error != nullptr)
    *error = message;
  return false;
}

bool readJson(const fs::path& path, Json& value, std::string* error)
{
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return fail("failed to open JSON file '" + path.string() + "'", error);
  try
  {
    value = Json::parse(input);
    return true;
  }
  catch (const std::exception& exception)
  {
    return fail("invalid JSON '" + path.string() + "': " + exception.what(), error);
  }
}

bool writeJson(const fs::path& path, const Json& value, std::string* error)
{
  try
  {
    if (!path.parent_path().empty())
      fs::create_directories(path.parent_path());
    const fs::path temporary = path.string() + ".tmp";
    {
      std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
      if (!output)
        return fail("failed to open temporary JSON file '" + temporary.string() + "'", error);
      output << value.dump(2) << '\n';
      output.flush();
      if (!output)
        return fail("failed to write temporary JSON file '" + temporary.string() + "'", error);
    }
    fs::rename(temporary, path);
    return true;
  }
  catch (const std::exception& exception)
  {
    return fail("failed to write JSON file '" + path.string() + "': " + exception.what(), error);
  }
}

std::string utcNow()
{
  const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm tm = {};
#if defined(_WIN32)
  gmtime_s(&tm, &now);
#else
  gmtime_r(&now, &tm);
#endif
  std::ostringstream result;
  result << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return result.str();
}

uint32_t rotateRight(uint32_t value, uint32_t count)
{
  return (value >> count) | (value << (32u - count));
}

std::string sha256(const std::string& text)
{
  static const std::array<uint32_t, 64> constants = {
      0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
      0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
      0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
      0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
      0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
      0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
      0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
      0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
      0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
      0xc67178f2u};
  std::array<uint32_t, 8> digest = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
  std::vector<unsigned char> bytes(text.begin(), text.end());
  const uint64_t bit_length = static_cast<uint64_t>(bytes.size()) * 8u;
  bytes.push_back(0x80u);
  while (bytes.size() % 64u != 56u)
    bytes.push_back(0u);
  for (int shift = 56; shift >= 0; shift -= 8)
    bytes.push_back(static_cast<unsigned char>(bit_length >> shift));

  for (size_t offset = 0; offset < bytes.size(); offset += 64u)
  {
    std::array<uint32_t, 64> words = {};
    for (size_t index = 0; index < 16; ++index)
    {
      const size_t position = offset + index * 4u;
      words[index] = (static_cast<uint32_t>(bytes[position]) << 24u) |
                     (static_cast<uint32_t>(bytes[position + 1]) << 16u) |
                     (static_cast<uint32_t>(bytes[position + 2]) << 8u) |
                     static_cast<uint32_t>(bytes[position + 3]);
    }
    for (size_t index = 16; index < words.size(); ++index)
    {
      const uint32_t s0 = rotateRight(words[index - 15], 7u) ^ rotateRight(words[index - 15], 18u) ^
                          (words[index - 15] >> 3u);
      const uint32_t s1 = rotateRight(words[index - 2], 17u) ^ rotateRight(words[index - 2], 19u) ^
                          (words[index - 2] >> 10u);
      words[index] = words[index - 16] + s0 + words[index - 7] + s1;
    }
    uint32_t a = digest[0], b = digest[1], c = digest[2], d = digest[3];
    uint32_t e = digest[4], f = digest[5], g = digest[6], h = digest[7];
    for (size_t index = 0; index < words.size(); ++index)
    {
      const uint32_t sum1 = rotateRight(e, 6u) ^ rotateRight(e, 11u) ^ rotateRight(e, 25u);
      const uint32_t choice = (e & f) ^ (~e & g);
      const uint32_t temporary1 = h + sum1 + choice + constants[index] + words[index];
      const uint32_t sum0 = rotateRight(a, 2u) ^ rotateRight(a, 13u) ^ rotateRight(a, 22u);
      const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t temporary2 = sum0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + temporary1;
      d = c;
      c = b;
      b = a;
      a = temporary1 + temporary2;
    }
    digest[0] += a;
    digest[1] += b;
    digest[2] += c;
    digest[3] += d;
    digest[4] += e;
    digest[5] += f;
    digest[6] += g;
    digest[7] += h;
  }
  std::ostringstream result;
  result << std::hex << std::setfill('0');
  for (uint32_t word : digest)
    result << std::setw(8) << word;
  return result.str();
}

std::string jobFingerprint(const Json& job)
{
  return sha256(job.dump());
}

std::vector<fs::path> pathsFromJson(const Json& value, const fs::path& base)
{
  if (!value.is_array())
    throw std::domain_error("recording roles must be arrays");
  std::vector<fs::path> paths;
  for (const Json& entry : value)
  {
    fs::path path(entry.get<std::string>());
    if (path.is_relative())
      path = base / path;
    paths.push_back(path.lexically_normal());
  }
  return paths;
}

bool loadJob(const fs::path& path, Job& job, Json& source, std::string* error)
{
  if (!readJson(path, source, error))
    return false;
  try
  {
    if (source.at("schema").get<std::string>() != "libfreenect2.calibration-job" ||
        source.at("version").get<uint32_t>() != 1)
      return fail("unsupported calibration job version", error);
    Job loaded;
    loaded.path = fs::absolute(path).lexically_normal();
    const fs::path base = loaded.path.parent_path();
    const Json& recordings = source.at("recordings");
    loaded.color = pathsFromJson(recordings.value("color", Json::array()), base);
    loaded.ir = pathsFromJson(recordings.value("ir", Json::array()), base);
    loaded.stereo = pathsFromJson(recordings.value("stereo", Json::array()), base);
    loaded.depth = pathsFromJson(recordings.value("depth", Json::array()), base);
    const Json& board = source.at("board");
    if (board.at("type").get<std::string>() != "chessboard")
      return fail("0.4 supports chessboard calibration jobs only", error);
    loaded.columns = board.at("columns").get<int>();
    loaded.rows = board.at("rows").get<int>();
    loaded.square_size_mm = board.at("square_size_mm").get<double>();
    const std::string model = source.value("distortion_model", "brown_conrady_5");
    if (model == "brown_conrady_5")
      loaded.distortion_model = lf::DistortionModel::BrownConrady5;
    else if (model == "rational_8")
      loaded.distortion_model = lf::DistortionModel::Rational8;
    else
      return fail("unsupported calibration distortion model", error);
    if (source.contains("quality"))
    {
      const Json& quality = source.at("quality");
      loaded.gates.minimum_views = quality.value("minimum_views", 20u);
      loaded.gates.maximum_intrinsic_rms_px = quality.value("maximum_intrinsic_rms_px", 1.0);
      loaded.gates.maximum_stereo_rms_px = quality.value("maximum_stereo_rms_px", 1.5);
      loaded.gates.maximum_depth_rmse_mm = quality.value("maximum_depth_rmse_mm", 20.0);
    }
    if (loaded.columns < 2 || loaded.rows < 2 || !std::isfinite(loaded.square_size_mm) ||
        loaded.square_size_mm <= 0.0 || loaded.gates.minimum_views == 0)
      return fail("calibration board or quality settings are invalid", error);
    for (const std::vector<fs::path>* role :
         {&loaded.color, &loaded.ir, &loaded.stereo, &loaded.depth})
    {
      for (const fs::path& recording : *role)
      {
        if (!fs::is_directory(recording))
          return fail("recording directory does not exist: '" + recording.string() + "'", error);
      }
    }
    job = loaded;
    return true;
  }
  catch (const std::exception& exception)
  {
    return fail(std::string("invalid calibration job: ") + exception.what(), error);
  }
}

Arguments parseArguments(int argc, char** argv)
{
  if (argc < 2)
    throw std::domain_error("a command is required");
  Arguments parsed;
  parsed.command = argv[1];
  for (int index = 2; index < argc; ++index)
  {
    const std::string argument = argv[index];
    if (argument == "--allow-low-quality")
      parsed.allow_low_quality = true;
    else if (argument == "--allow-serial-mismatch")
      parsed.allow_serial_mismatch = true;
    else if (argument.rfind("--", 0) == 0 && index + 1 < argc)
      parsed.values[argument.substr(2)] = argv[++index];
    else
      throw std::domain_error("unexpected argument: " + argument);
  }
  return parsed;
}

fs::path requiredPath(const Arguments& arguments, const std::string& name)
{
  const auto value = arguments.values.find(name);
  if (value == arguments.values.end() || value->second.empty())
    throw std::domain_error("--" + name + " is required");
  return value->second;
}

std::vector<cv::Point3f> boardPoints(const Job& job)
{
  std::vector<cv::Point3f> points;
  points.reserve(static_cast<size_t>(job.columns * job.rows));
  for (int row = 0; row < job.rows; ++row)
    for (int column = 0; column < job.columns; ++column)
      points.emplace_back(static_cast<float>(column * job.square_size_mm),
                          static_cast<float>(row * job.square_size_mm), 0.0f);
  return points;
}

cv::Mat grayColor(const lf::Frame& frame)
{
  if (frame.data == nullptr || (frame.format != lf::Frame::BGRX && frame.format != lf::Frame::RGBX))
    return {};
  cv::Mat four(static_cast<int>(frame.height), static_cast<int>(frame.width), CV_8UC4, frame.data);
  cv::Mat gray;
  cv::cvtColor(four, gray,
               frame.format == lf::Frame::BGRX ? cv::COLOR_BGRA2GRAY : cv::COLOR_RGBA2GRAY);
  return gray;
}

cv::Mat grayIr(const lf::Frame& frame)
{
  if (frame.data == nullptr || frame.format != lf::Frame::Float)
    return {};
  cv::Mat floating(static_cast<int>(frame.height), static_cast<int>(frame.width), CV_32F,
                   frame.data);
  cv::Mat gray;
  floating.convertTo(gray, CV_8U, 255.0 / 65535.0);
  cv::equalizeHist(gray, gray);
  return gray;
}

bool detectBoard(const cv::Mat& gray, const Job& job, std::vector<cv::Point2f>& points)
{
  if (gray.empty())
    return false;
  const cv::Size dimensions(job.columns, job.rows);
#if CV_VERSION_MAJOR >= 4
  if (cv::findChessboardCornersSB(gray, dimensions, points,
                                  cv::CALIB_CB_NORMALIZE_IMAGE | cv::CALIB_CB_EXHAUSTIVE))
    return true;
#endif
  if (!cv::findChessboardCorners(gray, dimensions, points,
                                 cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE))
    return false;
  cv::cornerSubPix(gray, points, cv::Size(5, 5), cv::Size(-1, -1),
                   cv::TermCriteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 40, 0.001));
  return true;
}

bool diverse(const std::vector<cv::Point2f>& previous, const std::vector<cv::Point2f>& current)
{
  if (previous.size() != current.size() || previous.empty())
    return true;
  double displacement = 0.0;
  for (size_t index = 0; index < current.size(); ++index)
    displacement += cv::norm(current[index] - previous[index]);
  return displacement / current.size() >= 8.0;
}

Json pointsJson(const std::vector<cv::Point2f>& points)
{
  Json result = Json::array();
  for (const cv::Point2f& point : points)
    result.push_back({point.x, point.y});
  return result;
}

std::vector<cv::Point2f> pointsFromJson(const Json& value)
{
  std::vector<cv::Point2f> result;
  for (const Json& point : value)
  {
    if (!point.is_array() || point.size() != 2)
      throw std::domain_error("cached chessboard point has invalid dimensions");
    result.emplace_back(point[0].get<float>(), point[1].get<float>());
  }
  return result;
}

struct DepthObservation
{
  double median_mm = 0.0;
  double mad_mm = 0.0;
  uint32_t valid_pixels = 0;
};

double medianValue(std::vector<double> values)
{
  if (values.empty())
    return 0.0;
  const size_t middle = values.size() / 2;
  std::nth_element(values.begin(), values.begin() + middle, values.end());
  return values[middle];
}

DepthObservation measureDepth(const lf::Frame& frame, const std::vector<cv::Point2f>& corners)
{
  DepthObservation observation;
  if (frame.data == nullptr || frame.format != lf::Frame::Float || corners.empty())
    return observation;
  cv::Rect bounds = cv::boundingRect(corners);
  bounds.x += bounds.width / 10;
  bounds.y += bounds.height / 10;
  bounds.width -= bounds.width / 5;
  bounds.height -= bounds.height / 5;
  bounds &= cv::Rect(0, 0, static_cast<int>(frame.width), static_cast<int>(frame.height));
  if (bounds.empty())
    return observation;
  const float* pixels = reinterpret_cast<const float*>(frame.data);
  std::vector<double> valid;
  for (int row = bounds.y; row < bounds.y + bounds.height; ++row)
  {
    for (int column = bounds.x; column < bounds.x + bounds.width; ++column)
    {
      const float value = pixels[static_cast<size_t>(row) * frame.width + column];
      if (std::isfinite(value) && value > 0.0f)
        valid.push_back(value);
    }
  }
  if (valid.empty())
    return observation;
  observation.median_mm = medianValue(valid);
  for (double& value : valid)
    value = std::fabs(value - observation.median_mm);
  observation.mad_mm = medianValue(valid);
  observation.valid_pixels = static_cast<uint32_t>(valid.size());
  return observation;
}

bool updateIdentity(lf::Freenect2Device& device, std::string& serial, std::string& firmware,
                    std::string* error)
{
  if (serial.empty())
  {
    serial = device.getSerialNumber();
    firmware = device.getFirmwareVersion();
    return true;
  }
  if (serial != device.getSerialNumber())
    return fail("calibration recordings contain different device serials", error);
  return true;
}

enum class DetectionRole
{
  Color,
  Ir,
  Stereo,
  Depth
};

bool detectRecording(const fs::path& path, DetectionRole role, const Job& job, Json& state,
                     std::string& serial, std::string& firmware, std::string* error)
{
  lf::Freenect2Replay replay;
  lf::ReplayOptions replay_options;
  lf::Freenect2Device* device =
      replay.openRecording(path.string(), new lf::CpuPacketPipeline(), replay_options);
  if (device == nullptr)
    return fail("failed to open recording: '" + path.string() + "'", error);
  if (!updateIdentity(*device, serial, firmware, error))
    return false;

  const bool needs_color = role == DetectionRole::Color || role == DetectionRole::Stereo;
  const bool needs_ir = role != DetectionRole::Color;
  const bool needs_depth = role == DetectionRole::Depth;
  unsigned int types = 0;
  if (needs_color)
    types |= lf::Frame::Color;
  if (needs_ir)
    types |= lf::Frame::Ir;
  if (needs_depth)
    types |= lf::Frame::Depth;
  lf::TimestampAlignedFrameListener listener(types, 80, 64);
  if (needs_color)
    device->setColorFrameListener(&listener);
  if (needs_ir || needs_depth)
    device->setIrAndDepthFrameListener(&listener);
  if (!device->startStreams(needs_color, needs_ir || needs_depth))
    return fail("failed to start replay: '" + path.string() + "'", error);

  std::vector<cv::Point2f> previous_color;
  std::vector<cv::Point2f> previous_ir;
  size_t accepted = 0;
  while (accepted < 300)
  {
    lf::FrameMap frames;
    if (!listener.waitForNewFrame(frames, 100))
    {
      if (device->getState() != lf::DeviceStreaming)
        break;
      continue;
    }
    std::vector<cv::Point2f> color_points;
    std::vector<cv::Point2f> ir_points;
    const bool found_color =
        !needs_color || detectBoard(grayColor(*frames[lf::Frame::Color]), job, color_points);
    const bool found_ir = !needs_ir || detectBoard(grayIr(*frames[lf::Frame::Ir]), job, ir_points);
    bool accept = found_color && found_ir;
    if (accept && needs_color)
      accept = diverse(previous_color, color_points);
    if (accept && needs_ir)
      accept = diverse(previous_ir, ir_points);
    if (accept)
    {
      Json view;
      view["recording"] = path.filename().string();
      const lf::Frame* sequence_frame =
          needs_color ? frames[lf::Frame::Color] : frames[lf::Frame::Ir];
      view["sequence"] = sequence_frame->sequence;
      if (needs_color)
      {
        view["color_points"] = pointsJson(color_points);
        view["color_size"] = {sequence_frame->width, sequence_frame->height};
        previous_color = color_points;
      }
      if (needs_ir)
      {
        view["ir_points"] = pointsJson(ir_points);
        view["ir_size"] = {frames[lf::Frame::Ir]->width, frames[lf::Frame::Ir]->height};
        previous_ir = ir_points;
      }
      if (needs_depth)
      {
        const DepthObservation depth = measureDepth(*frames[lf::Frame::Depth], ir_points);
        view["measured_depth_mm"] = depth.median_mm;
        view["depth_mad_mm"] = depth.mad_mm;
        view["valid_depth_pixels"] = depth.valid_pixels;
      }
      const char* key = role == DetectionRole::Color    ? "color_views"
                        : role == DetectionRole::Ir     ? "ir_views"
                        : role == DetectionRole::Stereo ? "stereo_views"
                                                        : "depth_views";
      state[key].push_back(view);
      ++accepted;
    }
    listener.release(frames);
  }
  device->stop();
  device->close();
  std::cout << path << ": accepted " << accepted << " views\n";
  return true;
}

bool detectAll(const Job& job, const Json& job_source, Json& state, std::string* error)
{
  state = {{"schema", "libfreenect2.calibration-state"},
           {"version", 1},
           {"job_fingerprint", jobFingerprint(job_source)},
           {"color_views", Json::array()},
           {"ir_views", Json::array()},
           {"stereo_views", Json::array()},
           {"depth_views", Json::array()}};
  std::string serial;
  std::string firmware;
  for (const fs::path& path : job.color)
    if (!detectRecording(path, DetectionRole::Color, job, state, serial, firmware, error))
      return false;
  for (const fs::path& path : job.ir)
    if (!detectRecording(path, DetectionRole::Ir, job, state, serial, firmware, error))
      return false;
  for (const fs::path& path : job.stereo)
    if (!detectRecording(path, DetectionRole::Stereo, job, state, serial, firmware, error))
      return false;
  for (const fs::path& path : job.depth)
    if (!detectRecording(path, DetectionRole::Depth, job, state, serial, firmware, error))
      return false;
  if (serial.empty())
    return fail("calibration job contains no recordings", error);
  state["device"] = {{"serial", serial}, {"firmware", firmware}};
  return true;
}

std::vector<std::vector<cv::Point2f>> collectPoints(const Json& direct, const Json& stereo,
                                                    const Json& depth, const char* key)
{
  std::vector<std::vector<cv::Point2f>> result;
  for (const Json* group : {&direct, &stereo, &depth})
  {
    for (const Json& view : *group)
    {
      if (view.contains(key))
        result.push_back(pointsFromJson(view.at(key)));
    }
  }
  return result;
}

std::vector<size_t> trainingIndices(size_t count)
{
  std::vector<size_t> indices;
  for (size_t index = 0; index < count; ++index)
    if (count < 5 || index % 5 != 0)
      indices.push_back(index);
  return indices;
}

double reprojectionRms(const std::vector<cv::Point3f>& object,
                       const std::vector<std::vector<cv::Point2f>>& observations,
                       const cv::Mat& camera, const cv::Mat& distortion)
{
  double squared = 0.0;
  size_t count = 0;
  for (size_t index = 0; index < observations.size(); ++index)
  {
    if (observations.size() >= 5 && index % 5 != 0)
      continue;
    cv::Mat rotation;
    cv::Mat translation;
    if (!cv::solvePnP(object, observations[index], camera, distortion, rotation, translation))
      continue;
    std::vector<cv::Point2f> projected;
    cv::projectPoints(object, rotation, translation, camera, distortion, projected);
    for (size_t point = 0; point < projected.size(); ++point)
    {
      const cv::Point2f delta = projected[point] - observations[index][point];
      squared += delta.dot(delta);
      ++count;
    }
  }
  return count == 0 ? std::numeric_limits<double>::infinity() : std::sqrt(squared / count);
}

double viewReprojectionRms(const std::vector<cv::Point3f>& object,
                           const std::vector<cv::Point2f>& observation, const cv::Mat& camera,
                           const cv::Mat& distortion)
{
  cv::Mat rotation;
  cv::Mat translation;
  if (!cv::solvePnP(object, observation, camera, distortion, rotation, translation))
    return std::numeric_limits<double>::infinity();
  std::vector<cv::Point2f> projected;
  cv::projectPoints(object, rotation, translation, camera, distortion, projected);
  double squared = 0.0;
  for (size_t index = 0; index < projected.size(); ++index)
  {
    const cv::Point2f delta = projected[index] - observation[index];
    squared += delta.dot(delta);
  }
  return std::sqrt(squared / projected.size());
}

double stereoHoldoutRms(const std::vector<cv::Point3f>& board, const Json& stereo_views,
                        const cv::Mat& ir_camera, const cv::Mat& ir_distortion,
                        const cv::Mat& color_camera, const cv::Mat& color_distortion,
                        const cv::Mat& ir_to_color_rotation, const cv::Mat& ir_to_color_translation)
{
  double squared = 0.0;
  size_t count = 0;
  for (size_t view_index = 0; view_index < stereo_views.size(); ++view_index)
  {
    if (stereo_views.size() < 5 || view_index % 5 != 0)
      continue;
    const std::vector<cv::Point2f> ir_points =
        pointsFromJson(stereo_views[view_index].at("ir_points"));
    const std::vector<cv::Point2f> color_points =
        pointsFromJson(stereo_views[view_index].at("color_points"));
    cv::Mat board_rotation_vector;
    cv::Mat board_translation;
    if (!cv::solvePnP(board, ir_points, ir_camera, ir_distortion, board_rotation_vector,
                      board_translation))
      continue;
    cv::Mat board_rotation;
    cv::Rodrigues(board_rotation_vector, board_rotation);
    std::vector<cv::Point3f> color_space;
    color_space.reserve(board.size());
    for (const cv::Point3f& point : board)
    {
      cv::Mat object(3, 1, CV_64F);
      object.at<double>(0) = point.x;
      object.at<double>(1) = point.y;
      object.at<double>(2) = point.z;
      const cv::Mat transformed =
          ir_to_color_rotation * (board_rotation * object + board_translation) +
          ir_to_color_translation;
      color_space.emplace_back(static_cast<float>(transformed.at<double>(0)),
                               static_cast<float>(transformed.at<double>(1)),
                               static_cast<float>(transformed.at<double>(2)));
    }
    std::vector<cv::Point2f> projected;
    cv::projectPoints(color_space, cv::Vec3d(0.0, 0.0, 0.0), cv::Vec3d(0.0, 0.0, 0.0), color_camera,
                      color_distortion, projected);
    for (size_t point = 0; point < projected.size(); ++point)
    {
      const cv::Point2f delta = projected[point] - color_points[point];
      squared += delta.dot(delta);
      ++count;
    }
  }
  return count == 0 ? std::numeric_limits<double>::infinity() : std::sqrt(squared / count);
}

bool calibrateIntrinsics(const std::vector<cv::Point3f>& board,
                         const std::vector<std::vector<cv::Point2f>>& observations,
                         const cv::Size& size, bool rational, cv::Mat& camera, cv::Mat& distortion,
                         double& rms, std::string* error)
{
  if (observations.size() < 3)
    return fail("at least three views are required to solve camera intrinsics", error);
  const std::vector<size_t> indices = trainingIndices(observations.size());
  std::vector<std::vector<cv::Point3f>> objects(indices.size(), board);
  std::vector<std::vector<cv::Point2f>> images;
  for (size_t index : indices)
    images.push_back(observations[index]);
  camera = cv::Mat::eye(3, 3, CV_64F);
  distortion = cv::Mat::zeros(1, rational ? 8 : 5, CV_64F);
  std::vector<cv::Mat> rotations;
  std::vector<cv::Mat> translations;
  const int flags = rational ? cv::CALIB_RATIONAL_MODEL : 0;
  const cv::TermCriteria criteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 100, 1e-9);
  rms = cv::calibrateCamera(objects, images, size, camera, distortion, rotations, translations,
                            flags, criteria);
  if (!std::isfinite(rms))
    return fail("intrinsic calibration produced a non-finite error", error);

  std::vector<double> errors;
  errors.reserve(images.size());
  for (const auto& observation : images)
    errors.push_back(viewReprojectionRms(board, observation, camera, distortion));
  const double center = medianValue(errors);
  std::vector<double> deviations = errors;
  for (double& value : deviations)
    value = std::fabs(value - center);
  const double threshold = center + std::max(0.05, 3.0 * 1.4826 * medianValue(deviations));
  std::vector<std::vector<cv::Point3f>> filtered_objects;
  std::vector<std::vector<cv::Point2f>> filtered_images;
  for (size_t index = 0; index < errors.size(); ++index)
  {
    if (std::isfinite(errors[index]) && errors[index] <= threshold)
    {
      filtered_objects.push_back(board);
      filtered_images.push_back(images[index]);
    }
  }
  if (filtered_images.size() >= 3 && filtered_images.size() < images.size())
  {
    camera = cv::Mat::eye(3, 3, CV_64F);
    distortion = cv::Mat::zeros(1, rational ? 8 : 5, CV_64F);
    rotations.clear();
    translations.clear();
    rms = cv::calibrateCamera(filtered_objects, filtered_images, size, camera, distortion,
                              rotations, translations, flags, criteria);
    if (!std::isfinite(rms))
      return fail("outlier-filtered intrinsic calibration produced a non-finite error", error);
  }
  return true;
}

lf::ProjectiveCameraModel modelFromCv(const cv::Size& size, const cv::Mat& camera,
                                      const cv::Mat& distortion, bool rational)
{
  lf::ProjectiveCameraModel model;
  model.width = static_cast<uint32_t>(size.width);
  model.height = static_cast<uint32_t>(size.height);
  model.fx = camera.at<double>(0, 0);
  model.fy = camera.at<double>(1, 1);
  model.cx = camera.at<double>(0, 2);
  model.cy = camera.at<double>(1, 2);
  model.distortion_model =
      rational ? lf::DistortionModel::Rational8 : lf::DistortionModel::BrownConrady5;
  for (size_t index = 0; index < static_cast<size_t>(distortion.total()) && index < 8; ++index)
    model.distortion[index] = distortion.at<double>(static_cast<int>(index));
  return model;
}

Json modelJson(const lf::ProjectiveCameraModel& model)
{
  Json coefficients = Json::array();
  const size_t count = model.distortion_model == lf::DistortionModel::Rational8 ? 8 : 5;
  for (size_t index = 0; index < count; ++index)
    coefficients.push_back(model.distortion[index]);
  return {{"width", model.width},
          {"height", model.height},
          {"fx", model.fx},
          {"fy", model.fy},
          {"cx", model.cx},
          {"cy", model.cy},
          {"distortion_model", model.distortion_model == lf::DistortionModel::Rational8
                                   ? "rational_8"
                                   : "brown_conrady_5"},
          {"distortion", coefficients}};
}

lf::ProjectiveCameraModel modelFromState(const Json& value)
{
  lf::ProjectiveCameraModel model;
  model.width = value.at("width").get<uint32_t>();
  model.height = value.at("height").get<uint32_t>();
  model.fx = value.at("fx").get<double>();
  model.fy = value.at("fy").get<double>();
  model.cx = value.at("cx").get<double>();
  model.cy = value.at("cy").get<double>();
  model.distortion_model = value.at("distortion_model") == "rational_8"
                               ? lf::DistortionModel::Rational8
                               : lf::DistortionModel::BrownConrady5;
  for (size_t index = 0; index < value.at("distortion").size(); ++index)
    model.distortion[index] = value.at("distortion")[index].get<double>();
  return model;
}

double depthRmse(const lf::DepthCorrectionProfile& profile,
                 const std::vector<lf::DepthCalibrationSample>& samples)
{
  if (samples.empty())
    return std::numeric_limits<double>::infinity();
  double squared = 0.0;
  for (const auto& sample : samples)
  {
    const double residual =
        profile.scale * sample.measured_median_mm + profile.offset_mm - sample.known_distance_mm;
    squared += residual * residual;
  }
  return std::sqrt(squared / samples.size());
}

lf::DepthCorrectionProfile offsetFit(const std::vector<lf::DepthCalibrationSample>& samples)
{
  lf::DepthCorrectionProfile profile;
  profile.model = lf::DepthCorrectionProfile::OffsetOnly;
  profile.scale = 1.0;
  double offset = 0.0;
  for (const auto& sample : samples)
    offset += sample.known_distance_mm - sample.measured_median_mm;
  profile.offset_mm = offset / samples.size();
  profile.samples = samples;
  for (const auto& sample : samples)
    profile.residuals_mm.push_back(profile.scale * sample.measured_median_mm + profile.offset_mm -
                                   sample.known_distance_mm);
  profile.rmse_mm = depthRmse(profile, samples);
  return profile;
}

std::vector<lf::DepthCalibrationSample>
rejectDepthOutliers(const std::vector<lf::DepthCalibrationSample>& samples)
{
  if (samples.size() < 4)
    return samples;
  std::vector<double> offsets;
  offsets.reserve(samples.size());
  for (const auto& sample : samples)
    offsets.push_back(sample.known_distance_mm - sample.measured_median_mm);
  const double center = medianValue(offsets);
  std::vector<double> deviations = offsets;
  for (double& value : deviations)
    value = std::fabs(value - center);
  const double threshold = std::max(1.0, 3.0 * 1.4826 * medianValue(deviations));
  std::vector<lf::DepthCalibrationSample> filtered;
  for (size_t index = 0; index < samples.size(); ++index)
    if (std::fabs(offsets[index] - center) <= threshold)
      filtered.push_back(samples[index]);
  return filtered.empty() ? samples : filtered;
}

bool solveCalibration(const Job& job, Json& state, std::string* error)
{
  try
  {
    const std::vector<cv::Point3f> board = boardPoints(job);
    const Json& stereo_json = state.at("stereo_views");
    const Json& depth_json = state.at("depth_views");
    const auto color_points =
        collectPoints(state.at("color_views"), stereo_json, Json::array(), "color_points");
    const auto ir_points =
        collectPoints(state.at("ir_views"), stereo_json, depth_json, "ir_points");
    if (color_points.empty() || ir_points.empty() || stereo_json.empty())
      return fail("color, IR, and stereo observations are all required", error);

    const cv::Size color_size(state.contains("color_views") && !state.at("color_views").empty()
                                  ? state.at("color_views")[0].at("color_size")[0].get<int>()
                                  : stereo_json[0].at("color_size")[0].get<int>(),
                              state.contains("color_views") && !state.at("color_views").empty()
                                  ? state.at("color_views")[0].at("color_size")[1].get<int>()
                                  : stereo_json[0].at("color_size")[1].get<int>());
    const Json& first_ir = !state.at("ir_views").empty()
                               ? state.at("ir_views")[0]
                               : (!stereo_json.empty() ? stereo_json[0] : depth_json[0]);
    const cv::Size ir_size(first_ir.at("ir_size")[0].get<int>(),
                           first_ir.at("ir_size")[1].get<int>());
    const bool rational = job.distortion_model == lf::DistortionModel::Rational8;
    cv::Mat color_camera;
    cv::Mat color_distortion;
    cv::Mat ir_camera;
    cv::Mat ir_distortion;
    double color_rms = 0.0;
    double ir_rms = 0.0;
    if (!calibrateIntrinsics(board, color_points, color_size, rational, color_camera,
                             color_distortion, color_rms, error) ||
        !calibrateIntrinsics(board, ir_points, ir_size, rational, ir_camera, ir_distortion, ir_rms,
                             error))
      return false;

    std::vector<std::vector<cv::Point3f>> stereo_objects;
    std::vector<std::vector<cv::Point2f>> stereo_color;
    std::vector<std::vector<cv::Point2f>> stereo_ir;
    for (size_t index = 0; index < stereo_json.size(); ++index)
    {
      if (stereo_json.size() >= 5 && index % 5 == 0)
        continue;
      stereo_objects.push_back(board);
      stereo_color.push_back(pointsFromJson(stereo_json[index].at("color_points")));
      stereo_ir.push_back(pointsFromJson(stereo_json[index].at("ir_points")));
    }
    cv::Mat rotation;
    cv::Mat translation;
    cv::Mat essential;
    cv::Mat fundamental;
    const double stereo_rms = cv::stereoCalibrate(
        stereo_objects, stereo_ir, stereo_color, ir_camera, ir_distortion, color_camera,
        color_distortion, ir_size, rotation, translation, essential, fundamental,
        cv::CALIB_FIX_INTRINSIC,
        cv::TermCriteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 100, 1e-9));

    lf::CalibrationProfile profile;
    profile.setDeviceIdentity(state.at("device").at("serial").get<std::string>(),
                              state.at("device").at("firmware").get<std::string>());
    profile.setColorCamera(modelFromCv(color_size, color_camera, color_distortion, rational));
    profile.setIrCamera(modelFromCv(ir_size, ir_camera, ir_distortion, rational));
    lf::RigidTransform transform;
    for (size_t index = 0; index < 9; ++index)
      transform.rotation[index] =
          rotation.at<double>(static_cast<int>(index / 3), static_cast<int>(index % 3));
    for (size_t index = 0; index < 3; ++index)
      transform.translation_m[index] = translation.at<double>(static_cast<int>(index)) / 1000.0;
    profile.setDepthToColor(transform);

    std::vector<lf::DepthCalibrationSample> depth_training;
    std::vector<lf::DepthCalibrationSample> depth_holdout;
    for (size_t index = 0; index < depth_json.size(); ++index)
    {
      const double measured = depth_json[index].value("measured_depth_mm", 0.0);
      if (!std::isfinite(measured) || measured <= 0.0)
        continue;
      cv::Mat rvec;
      cv::Mat tvec;
      const std::vector<cv::Point2f> points = pointsFromJson(depth_json[index].at("ir_points"));
      if (!cv::solvePnP(board, points, ir_camera, ir_distortion, rvec, tvec))
        continue;
      cv::Mat rotation_board;
      cv::Rodrigues(rvec, rotation_board);
      const cv::Vec3d center((job.columns - 1) * job.square_size_mm * 0.5,
                             (job.rows - 1) * job.square_size_mm * 0.5, 0.0);
      cv::Mat center_mat(3, 1, CV_64F);
      center_mat.at<double>(0) = center[0];
      center_mat.at<double>(1) = center[1];
      center_mat.at<double>(2) = center[2];
      const cv::Mat camera_center = rotation_board * center_mat + tvec;
      lf::DepthCalibrationSample sample;
      sample.known_distance_mm = camera_center.at<double>(2);
      sample.measured_median_mm = measured;
      sample.mad_mm = depth_json[index].value("depth_mad_mm", 0.0);
      sample.valid_pixel_count = depth_json[index].value("valid_depth_pixels", 0u);
      (depth_json.size() >= 5 && index % 5 == 0 ? depth_holdout : depth_training).push_back(sample);
    }
    depth_training = rejectDepthOutliers(depth_training);
    if (depth_training.empty())
      return fail("no usable depth observations remain after solving board poses", error);
    double selected_depth_rmse = std::numeric_limits<double>::infinity();
    {
      lf::DepthCorrectionProfile offset = offsetFit(depth_training);
      lf::DepthCorrectionProfile linear;
      std::string fit_error;
      const bool has_linear = lf::fitDepthCorrectionProfile(depth_training, linear, &fit_error) &&
                              linear.model == lf::DepthCorrectionProfile::Linear;
      const auto& validation = depth_holdout.empty() ? depth_training : depth_holdout;
      const double offset_rmse = depthRmse(offset, validation);
      const double linear_rmse =
          has_linear ? depthRmse(linear, validation) : std::numeric_limits<double>::infinity();
      lf::DepthCorrectionProfile selected = linear_rmse <= offset_rmse * 0.8 ? linear : offset;
      selected.serial = profile.serial();
      selected.firmware = profile.firmware();
      selected.rmse_mm = linear_rmse <= offset_rmse * 0.8 ? linear_rmse : offset_rmse;
      selected_depth_rmse = selected.rmse_mm;
      profile.setDepthCorrection(selected);
    }

    lf::CalibrationQualityMetrics quality;
    quality.color_views = static_cast<uint32_t>(color_points.size());
    quality.ir_views = static_cast<uint32_t>(ir_points.size());
    quality.stereo_views = static_cast<uint32_t>(stereo_json.size());
    quality.depth_views = static_cast<uint32_t>(depth_training.size() + depth_holdout.size());
    quality.color_rms_px = color_rms;
    quality.ir_rms_px = ir_rms;
    const double stereo_holdout =
        stereoHoldoutRms(board, stereo_json, ir_camera, ir_distortion, color_camera,
                         color_distortion, rotation, translation);
    quality.held_out_stereo_rms_px = std::max(
        stereo_rms,
        std::max(std::isfinite(stereo_holdout) ? stereo_holdout : stereo_rms,
                 std::max(reprojectionRms(board, color_points, color_camera, color_distortion),
                          reprojectionRms(board, ir_points, ir_camera, ir_distortion))));
    quality.depth_rmse_mm = selected_depth_rmse;
    profile.setQualityMetrics(quality);
    profile.setProvenance(utcNow(), lf::getVersion(), state.value("job_fingerprint", ""));

    Json solution;
    solution["color"] = modelJson(profile.colorCamera());
    solution["ir"] = modelJson(profile.irCamera());
    solution["rotation"] = profile.depthToColor().rotation;
    solution["translation_m"] = profile.depthToColor().translation_m;
    if (profile.hasDepthCorrection())
      solution["depth_correction"] = {
          {"model", profile.depthCorrection().model == lf::DepthCorrectionProfile::Linear
                        ? "linear"
                        : "offset_only"},
          {"scale", profile.depthCorrection().scale},
          {"offset_mm", profile.depthCorrection().offset_mm},
          {"rmse_mm", profile.depthCorrection().rmse_mm}};
    solution["quality"] = {{"color_views", quality.color_views},
                           {"ir_views", quality.ir_views},
                           {"stereo_views", quality.stereo_views},
                           {"depth_views", quality.depth_views},
                           {"color_rms_px", quality.color_rms_px},
                           {"ir_rms_px", quality.ir_rms_px},
                           {"held_out_stereo_rms_px", quality.held_out_stereo_rms_px},
                           {"depth_rmse_mm", quality.depth_rmse_mm}};
    solution["created_utc"] = profile.createdUtc();
    state["solution"] = solution;
    return true;
  }
  catch (const cv::Exception& exception)
  {
    return fail(std::string("OpenCV calibration failed: ") + exception.what(), error);
  }
  catch (const std::exception& exception)
  {
    return fail(std::string("invalid calibration state: ") + exception.what(), error);
  }
}

bool profileFromState(const Json& state, lf::CalibrationProfile& profile, std::string* error)
{
  try
  {
    const Json& solution = state.at("solution");
    lf::CalibrationProfile loaded;
    loaded.setDeviceIdentity(state.at("device").at("serial").get<std::string>(),
                             state.at("device").at("firmware").get<std::string>());
    loaded.setColorCamera(modelFromState(solution.at("color")));
    loaded.setIrCamera(modelFromState(solution.at("ir")));
    lf::RigidTransform transform;
    for (size_t index = 0; index < 9; ++index)
      transform.rotation[index] = solution.at("rotation")[index].get<double>();
    for (size_t index = 0; index < 3; ++index)
      transform.translation_m[index] = solution.at("translation_m")[index].get<double>();
    loaded.setDepthToColor(transform);
    if (solution.contains("depth_correction"))
    {
      const Json& value = solution.at("depth_correction");
      lf::DepthCorrectionProfile correction;
      correction.serial = loaded.serial();
      correction.firmware = loaded.firmware();
      correction.model = value.at("model") == "linear" ? lf::DepthCorrectionProfile::Linear
                                                       : lf::DepthCorrectionProfile::OffsetOnly;
      correction.scale = value.at("scale").get<double>();
      correction.offset_mm = value.at("offset_mm").get<double>();
      correction.rmse_mm = value.at("rmse_mm").get<double>();
      loaded.setDepthCorrection(correction);
    }
    const Json& value = solution.at("quality");
    lf::CalibrationQualityMetrics quality;
    quality.color_views = value.at("color_views").get<uint32_t>();
    quality.ir_views = value.at("ir_views").get<uint32_t>();
    quality.stereo_views = value.at("stereo_views").get<uint32_t>();
    quality.depth_views = value.at("depth_views").get<uint32_t>();
    quality.color_rms_px = value.at("color_rms_px").get<double>();
    quality.ir_rms_px = value.at("ir_rms_px").get<double>();
    quality.held_out_stereo_rms_px = value.at("held_out_stereo_rms_px").get<double>();
    quality.depth_rmse_mm = value.at("depth_rmse_mm").get<double>();
    loaded.setQualityMetrics(quality);
    loaded.setProvenance(solution.value("created_utc", ""), lf::getVersion(),
                         state.value("job_fingerprint", ""));
    if (!loaded.isValid(error))
      return false;
    profile = std::move(loaded);
    return true;
  }
  catch (const std::exception& exception)
  {
    return fail(std::string("calibration state has no valid solution: ") + exception.what(), error);
  }
}

bool qualityPasses(const Job& job, const lf::CalibrationProfile& profile, std::string* error)
{
  const auto& quality = profile.qualityMetrics();
  std::ostringstream failures;
  if (quality.color_views < job.gates.minimum_views || quality.ir_views < job.gates.minimum_views ||
      quality.stereo_views < job.gates.minimum_views ||
      quality.depth_views < job.gates.minimum_views)
    failures << "fewer than " << job.gates.minimum_views << " usable views in one or more roles; ";
  if (quality.color_rms_px > job.gates.maximum_intrinsic_rms_px ||
      quality.ir_rms_px > job.gates.maximum_intrinsic_rms_px)
    failures << "intrinsic RMS exceeds " << job.gates.maximum_intrinsic_rms_px << " px; ";
  if (quality.held_out_stereo_rms_px > job.gates.maximum_stereo_rms_px)
    failures << "held-out/stereo RMS exceeds " << job.gates.maximum_stereo_rms_px << " px; ";
  if (!profile.hasDepthCorrection() || quality.depth_rmse_mm > job.gates.maximum_depth_rmse_mm)
    failures << "depth RMSE exceeds " << job.gates.maximum_depth_rmse_mm << " mm; ";
  if (!failures.str().empty())
    return fail(failures.str(), error);
  return true;
}

void printQuality(const lf::CalibrationProfile& profile)
{
  const auto& quality = profile.qualityMetrics();
  std::cout << "views color=" << quality.color_views << " ir=" << quality.ir_views
            << " stereo=" << quality.stereo_views << " depth=" << quality.depth_views << '\n'
            << "RMS color=" << quality.color_rms_px << "px ir=" << quality.ir_rms_px
            << "px stereo/held-out=" << quality.held_out_stereo_rms_px
            << "px depth=" << quality.depth_rmse_mm << "mm\n";
}

cv::Mat cameraMatrix(const lf::ProjectiveCameraModel& model)
{
  cv::Mat camera = cv::Mat::eye(3, 3, CV_64F);
  camera.at<double>(0, 0) = model.fx;
  camera.at<double>(0, 2) = model.cx;
  camera.at<double>(1, 1) = model.fy;
  camera.at<double>(1, 2) = model.cy;
  return camera;
}

cv::Mat distortionMatrix(const lf::ProjectiveCameraModel& model)
{
  const int count = model.distortion_model == lf::DistortionModel::Rational8 ? 8 : 5;
  cv::Mat distortion(1, count, CV_64F);
  for (int index = 0; index < count; ++index)
    distortion.at<double>(index) = model.distortion[static_cast<size_t>(index)];
  return distortion;
}

bool importYaml(const Arguments& arguments, std::string* error)
{
  const fs::path input = requiredPath(arguments, "input-dir");
  const fs::path output = requiredPath(arguments, "output");
  const auto serial_it = arguments.values.find("serial");
  if (serial_it == arguments.values.end() || serial_it->second.empty())
    return fail("--serial is required for YAML import", error);
  const std::string firmware = arguments.values.count("firmware")
                                   ? arguments.values.find("firmware")->second
                                   : std::string();
  cv::Mat color_camera, color_distortion, ir_camera, ir_distortion, rotation, translation;
  double depth_shift = 0.0;
  {
    cv::FileStorage file((input / "calib_color.yaml").string(), cv::FileStorage::READ);
    if (!file.isOpened())
      return fail("failed to open calib_color.yaml", error);
    file["cameraMatrix"] >> color_camera;
    file["distortionCoefficients"] >> color_distortion;
  }
  {
    cv::FileStorage file((input / "calib_ir.yaml").string(), cv::FileStorage::READ);
    if (!file.isOpened())
      return fail("failed to open calib_ir.yaml", error);
    file["cameraMatrix"] >> ir_camera;
    file["distortionCoefficients"] >> ir_distortion;
  }
  {
    cv::FileStorage file((input / "calib_pose.yaml").string(), cv::FileStorage::READ);
    if (!file.isOpened())
      return fail("failed to open calib_pose.yaml", error);
    file["rotation"] >> rotation;
    file["translation"] >> translation;
  }
  {
    cv::FileStorage file((input / "calib_depth.yaml").string(), cv::FileStorage::READ);
    if (!file.isOpened())
      return fail("failed to open calib_depth.yaml", error);
    file["depthShift"] >> depth_shift;
  }
  color_camera.convertTo(color_camera, CV_64F);
  color_distortion.convertTo(color_distortion, CV_64F);
  ir_camera.convertTo(ir_camera, CV_64F);
  ir_distortion.convertTo(ir_distortion, CV_64F);
  rotation.convertTo(rotation, CV_64F);
  translation.convertTo(translation, CV_64F);
  const bool color_rational = color_distortion.total() >= 8;
  const bool ir_rational = ir_distortion.total() >= 8;
  lf::CalibrationProfile profile;
  profile.setDeviceIdentity(serial_it->second, firmware);
  profile.setColorCamera(
      modelFromCv(cv::Size(1920, 1080), color_camera, color_distortion, color_rational));
  profile.setIrCamera(modelFromCv(cv::Size(512, 424), ir_camera, ir_distortion, ir_rational));
  lf::RigidTransform transform;
  for (size_t index = 0; index < 9; ++index)
    transform.rotation[index] =
        rotation.at<double>(static_cast<int>(index / 3), static_cast<int>(index % 3));
  for (size_t index = 0; index < 3; ++index)
    transform.translation_m[index] = translation.at<double>(static_cast<int>(index));
  profile.setDepthToColor(transform);
  lf::DepthCorrectionProfile correction;
  correction.serial = serial_it->second;
  correction.firmware = firmware;
  correction.scale = 1.0;
  correction.offset_mm = depth_shift;
  correction.rmse_mm = 0.0;
  profile.setDepthCorrection(correction);
  profile.setProvenance(utcNow(), lf::getVersion(), "legacy-yaml-import");
  return profile.save(output.string(), error);
}

bool exportYaml(const Arguments& arguments, std::string* error)
{
  lf::CalibrationProfile profile;
  if (!lf::CalibrationProfile::load(requiredPath(arguments, "profile").string(), profile, error))
    return false;
  const fs::path output = requiredPath(arguments, "output-dir");
  std::error_code filesystem_error;
  fs::create_directories(output, filesystem_error);
  if (filesystem_error)
    return fail("failed to create YAML output directory: " + filesystem_error.message(), error);
  if (profile.hasDepthCorrection() && std::fabs(profile.depthCorrection().scale - 1.0) > 1e-12)
    return fail("legacy YAML cannot represent a linear depth scale", error);

  const auto write_camera = [&](const fs::path& path, const lf::ProjectiveCameraModel& model)
  {
    cv::FileStorage file(path.string(), cv::FileStorage::WRITE);
    if (!file.isOpened())
      return false;
    const cv::Mat camera = cameraMatrix(model);
    cv::Mat projection = cv::Mat::zeros(3, 4, CV_64F);
    camera.copyTo(projection(cv::Rect(0, 0, 3, 3)));
    file << "cameraMatrix" << camera;
    file << "distortionCoefficients" << distortionMatrix(model);
    file << "rotation" << cv::Mat::eye(3, 3, CV_64F);
    file << "projection" << projection;
    return true;
  };
  if (!write_camera(output / "calib_color.yaml", profile.colorCamera()) ||
      !write_camera(output / "calib_ir.yaml", profile.irCamera()))
    return fail("failed to write legacy camera YAML", error);
  cv::Mat rotation(3, 3, CV_64F);
  cv::Mat translation(3, 1, CV_64F);
  for (size_t index = 0; index < 9; ++index)
    rotation.at<double>(static_cast<int>(index / 3), static_cast<int>(index % 3)) =
        profile.depthToColor().rotation[index];
  for (size_t index = 0; index < 3; ++index)
    translation.at<double>(static_cast<int>(index)) = profile.depthToColor().translation_m[index];
  cv::Mat skew = cv::Mat::zeros(3, 3, CV_64F);
  skew.at<double>(0, 1) = -translation.at<double>(2);
  skew.at<double>(0, 2) = translation.at<double>(1);
  skew.at<double>(1, 0) = translation.at<double>(2);
  skew.at<double>(1, 2) = -translation.at<double>(0);
  skew.at<double>(2, 0) = -translation.at<double>(1);
  skew.at<double>(2, 1) = translation.at<double>(0);
  const cv::Mat essential = skew * rotation;
  const cv::Mat fundamental = cameraMatrix(profile.colorCamera()).inv().t() * essential *
                              cameraMatrix(profile.irCamera()).inv();
  {
    cv::FileStorage file((output / "calib_pose.yaml").string(), cv::FileStorage::WRITE);
    if (!file.isOpened())
      return fail("failed to write calib_pose.yaml", error);
    file << "rotation" << rotation << "translation" << translation << "essential" << essential
         << "fundamental" << fundamental;
  }
  {
    cv::FileStorage file((output / "calib_depth.yaml").string(), cv::FileStorage::WRITE);
    if (!file.isOpened())
      return fail("failed to write calib_depth.yaml", error);
    file << "depthShift"
         << (profile.hasDepthCorrection() ? profile.depthCorrection().offset_mm : 0.0);
  }
  return true;
}

void printUsage()
{
  std::cout
      << "KinectCameraCalibration <command> [options]\n"
      << "  inspect  --job JOB\n"
      << "  detect   --job JOB --state STATE\n"
      << "  solve    --job JOB --state STATE\n"
      << "  validate --job JOB --state STATE --output PROFILE [--allow-low-quality]\n"
      << "  run      --job JOB --state STATE --output PROFILE [--allow-low-quality]\n"
      << "  import-yaml --input-dir DIR --serial SERIAL [--firmware VERSION] --output PROFILE\n"
      << "  export-yaml --profile PROFILE --output-dir DIR\n";
}

} // namespace

int main(int argc, char** argv)
{
  try
  {
    const Arguments arguments = parseArguments(argc, argv);
    std::string error;
    if (arguments.command == "import-yaml")
    {
      if (!importYaml(arguments, &error))
        throw std::runtime_error(error);
      return 0;
    }
    if (arguments.command == "export-yaml")
    {
      if (!exportYaml(arguments, &error))
        throw std::runtime_error(error);
      return 0;
    }

    Job job;
    Json job_source;
    if (!loadJob(requiredPath(arguments, "job"), job, job_source, &error))
      throw std::runtime_error(error);
    if (arguments.command == "inspect")
    {
      std::cout << "job valid: board=" << job.columns << 'x' << job.rows
                << " square=" << job.square_size_mm << "mm recordings color=" << job.color.size()
                << " ir=" << job.ir.size() << " stereo=" << job.stereo.size()
                << " depth=" << job.depth.size() << " sha256=" << jobFingerprint(job_source)
                << '\n';
      return 0;
    }

    const fs::path state_path = requiredPath(arguments, "state");
    Json state;
    if (arguments.command == "detect" || arguments.command == "run")
    {
      if (!detectAll(job, job_source, state, &error) || !writeJson(state_path, state, &error))
        throw std::runtime_error(error);
      if (arguments.command == "detect")
        return 0;
    }
    else if (!readJson(state_path, state, &error))
      throw std::runtime_error(error);

    if (arguments.command == "solve" || arguments.command == "run")
    {
      if (state.value("job_fingerprint", "") != jobFingerprint(job_source))
        throw std::runtime_error("calibration state does not match the job JSON");
      if (!solveCalibration(job, state, &error) || !writeJson(state_path, state, &error))
        throw std::runtime_error(error);
      if (arguments.command == "solve")
        return 0;
    }

    if (arguments.command == "validate" || arguments.command == "run")
    {
      lf::CalibrationProfile profile;
      if (!profileFromState(state, profile, &error))
        throw std::runtime_error(error);
      printQuality(profile);
      const bool passes = qualityPasses(job, profile, &error);
      if (!passes && !arguments.allow_low_quality)
        throw std::runtime_error("quality gates failed; profile withheld: " + error);
      if (!passes)
        std::cerr << "warning: quality gates overridden: " << error << '\n';
      if (!profile.save(requiredPath(arguments, "output").string(), &error))
        throw std::runtime_error(error);
      return 0;
    }

    printUsage();
    throw std::domain_error("unsupported command: " + arguments.command);
  }
  catch (const std::exception& exception)
  {
    std::cerr << "error: " << exception.what() << '\n';
    printUsage();
    return 2;
  }
}
