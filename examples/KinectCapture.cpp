/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * Copyright (c) 2014 individual OpenKinect contributors. See the CONTRIB file
 * for details.
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#include <libfreenect2/calibration_profile.h>
#include <libfreenect2/depth_calibration.h>
#include <libfreenect2/frame_listener_impl.h>
#include <libfreenect2/libfreenect2.hpp>
#include <libfreenect2/packet_pipeline.h>
#include <libfreenect2/projective_registration.h>
#include <libfreenect2/recording.h>
#include <libfreenect2/registration.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{

const uint32_t DEFAULT_MAX_DELTA_TICKS = 200;

bool parseUint32(const std::string& text, uint32_t& value)
{
  if (text.empty())
    return false;

  uint64_t parsed = 0;
  for (std::string::const_iterator character = text.begin(); character != text.end(); ++character)
  {
    if (!std::isdigit(static_cast<unsigned char>(*character)))
      return false;
    parsed = parsed * 10 + static_cast<unsigned int>(*character - '0');
    if (parsed > std::numeric_limits<uint32_t>::max())
      return false;
  }
  value = static_cast<uint32_t>(parsed);
  return true;
}

void printUsage()
{
  std::cerr << "usage: KinectCapture OUTPUT_DIRECTORY "
               "[--calibration-profile PROFILE.json] [--depth-correction PROFILE.json] "
               "[--allow-device-mismatch]\n"
            << "       KinectCapture snapshot OUTPUT_DIRECTORY "
               "[--max-delta-ticks TICKS] [--calibration-profile PROFILE.json] "
               "[--depth-correction PROFILE.json] "
               "[--allow-device-mismatch]\n"
            << "       KinectCapture record OUTPUT_DIRECTORY "
               "(--depth-frames N | --duration-seconds N) "
               "[--calibration-profile PROFILE.json] [--allow-device-mismatch]\n";
}

bool writeColorPpm(const std::string& path, const libfreenect2::Frame& frame)
{
  std::ofstream out(path, std::ios::binary);
  if (!out || frame.bytes_per_pixel < 4 ||
      (frame.format != libfreenect2::Frame::BGRX && frame.format != libfreenect2::Frame::RGBX))
    return false;

  out << "P6\n" << frame.width << " " << frame.height << "\n255\n";
  for (size_t i = 0; i < frame.width * frame.height; ++i)
  {
    const unsigned char* p = frame.data + i * frame.bytes_per_pixel;
    if (frame.format == libfreenect2::Frame::RGBX)
      out.write(reinterpret_cast<const char*>(p), 3);
    else
    {
      const unsigned char rgb[] = {p[2], p[1], p[0]};
      out.write(reinterpret_cast<const char*>(rgb), 3);
    }
  }
  out.flush();
  return out.good();
}

void hsvToRgb(float hue, unsigned char& r, unsigned char& g, unsigned char& b)
{
  const float c = 1.0f;
  const float h = hue / 60.0f;
  const float x = c * (1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f));
  float rf = 0.0f, gf = 0.0f, bf = 0.0f;
  if (h < 1.0f)
  {
    rf = c;
    gf = x;
  }
  else if (h < 2.0f)
  {
    rf = x;
    gf = c;
  }
  else if (h < 3.0f)
  {
    gf = c;
    bf = x;
  }
  else if (h < 4.0f)
  {
    gf = x;
    bf = c;
  }
  else if (h < 5.0f)
  {
    rf = x;
    bf = c;
  }
  else
  {
    rf = c;
    bf = x;
  }
  r = static_cast<unsigned char>(rf * 255.0f);
  g = static_cast<unsigned char>(gf * 255.0f);
  b = static_cast<unsigned char>(bf * 255.0f);
}

struct DepthStats
{
  size_t valid = 0;
  float minimum = std::numeric_limits<float>::infinity();
  float maximum = 0.0f;
  double sum = 0.0;
};

bool writeDepth(const std::string& raw_path, const std::string& visual_path,
                const libfreenect2::Frame& frame, DepthStats& stats)
{
  const float* depth = reinterpret_cast<const float*>(frame.data);
  std::ofstream raw(raw_path, std::ios::binary);
  std::ofstream visual(visual_path, std::ios::binary);
  if (!raw || !visual || frame.bytes_per_pixel != sizeof(float))
    return false;

  raw << "P5\n" << frame.width << " " << frame.height << "\n65535\n";
  visual << "P6\n" << frame.width << " " << frame.height << "\n255\n";

  for (size_t i = 0; i < frame.width * frame.height; ++i)
  {
    const float d = depth[i];
    const bool valid = std::isfinite(d) && d > 0.0f;
    const uint16_t millimeters = valid ? static_cast<uint16_t>(std::min(d, 65535.0f)) : 0;
    const unsigned char be[] = {static_cast<unsigned char>(millimeters >> 8),
                                static_cast<unsigned char>(millimeters & 0xff)};
    raw.write(reinterpret_cast<const char*>(be), 2);

    unsigned char rgb[] = {0, 0, 0};
    if (valid)
    {
      stats.valid++;
      stats.minimum = std::min(stats.minimum, d);
      stats.maximum = std::max(stats.maximum, d);
      stats.sum += d;
      const float normalized = std::max(0.0f, std::min(1.0f, (d - 500.0f) / 4000.0f));
      hsvToRgb(normalized * 240.0f, rgb[0], rgb[1], rgb[2]);
    }
    visual.write(reinterpret_cast<const char*>(rgb), 3);
  }
  raw.flush();
  visual.flush();
  return raw.good() && visual.good();
}

bool writeIrPgm(const std::string& path, const libfreenect2::Frame& frame)
{
  const float* ir = reinterpret_cast<const float*>(frame.data);
  if (frame.bytes_per_pixel != sizeof(float))
    return false;

  std::vector<float> finite;
  finite.reserve(frame.width * frame.height);
  for (size_t i = 0; i < frame.width * frame.height; ++i)
    if (std::isfinite(ir[i]) && ir[i] > 0.0f)
      finite.push_back(ir[i]);

  float low = 0.0f, high = 1.0f;
  if (!finite.empty())
  {
    const size_t low_index = finite.size() / 100;
    const size_t high_index = finite.size() * 99 / 100;
    std::nth_element(finite.begin(), finite.begin() + low_index, finite.end());
    low = finite[low_index];
    std::nth_element(finite.begin(), finite.begin() + high_index, finite.end());
    high = std::max(low + 1.0f, finite[high_index]);
  }

  std::ofstream out(path, std::ios::binary);
  if (!out)
    return false;

  out << "P5\n" << frame.width << " " << frame.height << "\n255\n";
  for (size_t i = 0; i < frame.width * frame.height; ++i)
  {
    float normalized = 0.0f;
    if (std::isfinite(ir[i]) && ir[i] > 0.0f)
    {
      normalized = (ir[i] - low) / (high - low);
      normalized = std::max(0.0f, std::min(1.0f, normalized));
    }
    const unsigned char value = static_cast<unsigned char>(std::sqrt(normalized) * 255.0f);
    out.write(reinterpret_cast<const char*>(&value), 1);
  }
  out.flush();
  return out.good();
}

} // namespace

int main(int argc, char** argv)
{
  bool timestamp_aligned = false;
  bool recording_mode = false;
  uint32_t max_delta_ticks = DEFAULT_MAX_DELTA_TICKS;
  uint32_t recording_depth_frames = 0;
  uint32_t recording_duration_seconds = 0;
  std::string calibration_profile_path;
  std::string correction_profile_path;
  bool allow_device_mismatch = false;
  std::string output;
  int capture_option_start = 0;
  if (argc >= 2 && std::string(argv[1]) == "record")
  {
    if (argc < 5)
    {
      printUsage();
      return 2;
    }
    recording_mode = true;
    output = argv[2];
    for (int argument = 3; argument < argc; ++argument)
    {
      const std::string option = argv[argument];
      if (option == "--depth-frames" && argument + 1 < argc && recording_duration_seconds == 0 &&
          recording_depth_frames == 0)
      {
        if (!parseUint32(argv[++argument], recording_depth_frames) || recording_depth_frames == 0)
        {
          printUsage();
          return 2;
        }
      }
      else if (option == "--duration-seconds" && argument + 1 < argc &&
               recording_depth_frames == 0 && recording_duration_seconds == 0)
      {
        if (!parseUint32(argv[++argument], recording_duration_seconds) ||
            recording_duration_seconds == 0)
        {
          printUsage();
          return 2;
        }
      }
      else if (option == "--calibration-profile" && argument + 1 < argc)
      {
        calibration_profile_path = argv[++argument];
        if (calibration_profile_path.empty())
        {
          printUsage();
          return 2;
        }
      }
      else if (option == "--allow-device-mismatch")
      {
        allow_device_mismatch = true;
      }
      else
      {
        printUsage();
        return 2;
      }
    }
    if ((recording_depth_frames == 0) == (recording_duration_seconds == 0) ||
        (allow_device_mismatch && calibration_profile_path.empty()))
    {
      printUsage();
      return 2;
    }
  }
  else if (argc >= 2 && std::string(argv[1]) == "snapshot")
  {
    if (argc < 3)
    {
      printUsage();
      return 2;
    }
    timestamp_aligned = true;
    output = argv[2];
    capture_option_start = 3;
  }
  else if (argc >= 2)
  {
    output = argv[1];
    capture_option_start = 2;
  }
  else
  {
    printUsage();
    return 2;
  }

  if (!recording_mode)
  {
    for (int argument = capture_option_start; argument < argc; ++argument)
    {
      const std::string option = argv[argument];
      if (option == "--max-delta-ticks" && timestamp_aligned && argument + 1 < argc)
      {
        if (!parseUint32(argv[++argument], max_delta_ticks))
        {
          printUsage();
          return 2;
        }
      }
      else if (option == "--depth-correction" && argument + 1 < argc)
      {
        correction_profile_path = argv[++argument];
        if (correction_profile_path.empty())
        {
          printUsage();
          return 2;
        }
      }
      else if (option == "--calibration-profile" && argument + 1 < argc)
      {
        calibration_profile_path = argv[++argument];
        if (calibration_profile_path.empty())
        {
          printUsage();
          return 2;
        }
      }
      else if (option == "--allow-device-mismatch")
      {
        allow_device_mismatch = true;
      }
      else
      {
        printUsage();
        return 2;
      }
    }
    if (allow_device_mismatch && correction_profile_path.empty() &&
        calibration_profile_path.empty())
    {
      printUsage();
      return 2;
    }
  }

  libfreenect2::CalibrationProfile calibration_profile;
  const bool use_calibration_profile = !calibration_profile_path.empty();
  if (use_calibration_profile)
  {
    std::string error;
    if (!libfreenect2::CalibrationProfile::load(calibration_profile_path, calibration_profile,
                                                &error))
    {
      std::cerr << "Unable to load calibration profile: " << error << "\n";
      return 2;
    }
  }

  libfreenect2::DepthCorrectionProfile correction_profile;
  const bool apply_depth_correction = !correction_profile_path.empty();
  if (apply_depth_correction)
  {
    std::string error;
    if (!libfreenect2::DepthCorrectionProfile::load(correction_profile_path, correction_profile,
                                                    &error))
    {
      std::cerr << "Unable to load depth correction profile: " << error << "\n";
      return 2;
    }
  }

  std::ofstream output_probe;
  if (!recording_mode)
    output_probe.open((output + "/metadata.json").c_str(), std::ios::app);
  if (!recording_mode && !output_probe)
  {
    std::cerr << "Output directory does not exist or is not writable: " << output << "\n";
    return 2;
  }
  output_probe.close();

  libfreenect2::Freenect2 freenect2;
  if (freenect2.enumerateDevices() == 0)
  {
    std::cerr << "No Kinect v2 detected\n";
    return 1;
  }
  const std::string serial = freenect2.getDefaultDeviceSerialNumber();

  libfreenect2::PacketPipeline* pipeline = 0;
  const char* pipeline_name = 0;
  if (recording_mode)
  {
    pipeline = new libfreenect2::DumpPacketPipeline();
    pipeline_name = "Dump";
  }
  else
  {
#ifdef LIBFREENECT2_WITH_METAL_SUPPORT
    pipeline = new libfreenect2::MetalPacketPipeline();
    pipeline_name = "Metal";
#else
    pipeline = new libfreenect2::CpuPacketPipeline();
    pipeline_name = "CPU";
#endif
  }

  // openDevice transfers pipeline ownership to its internal device object.
  libfreenect2::Freenect2Device* device = freenect2.openDevice(serial, pipeline);
  if (!device)
  {
    std::cerr << "Unable to open Kinect " << serial << "\n";
    return 1;
  }
  std::string firmware;
  bool correction_device_match = true;

  if (recording_mode)
  {
    libfreenect2::RecordingWriter writer(output, 64);
    if (!writer.isOpen())
    {
      std::cerr << "Unable to create recording: " << writer.getLastError() << "\n";
      device->close();
      return 1;
    }
    device->setColorFrameListener(&writer);
    device->setIrAndDepthFrameListener(&writer);
    if (!device->start())
    {
      std::cerr << "Unable to start Kinect streams\n";
      device->close();
      return 1;
    }
    firmware = device->getFirmwareVersion();

    libfreenect2::CalibrationData calibration;
    if (!device->getCalibrationData(calibration) ||
        !writer.setCalibration(serial, firmware, calibration))
    {
      std::cerr << "Unable to publish recording calibration: " << writer.getLastError() << "\n";
      device->stop();
      device->close();
      return 1;
    }
    if (use_calibration_profile)
    {
      std::string warning;
      std::string error;
      if (!calibration_profile.matchesDevice(serial, firmware, allow_device_mismatch, &warning,
                                             &error) ||
          !writer.setCalibrationProfile(calibration_profile, allow_device_mismatch))
      {
        if (error.empty())
          error = writer.getLastError();
        std::cerr << "Unable to attach calibration profile: " << error << "\n";
        device->stop();
        device->close();
        writer.close();
        return 2;
      }
      if (!warning.empty())
        std::cerr << "Warning: " << warning << "\n";
    }

    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(recording_duration_seconds);
    while (writer.isOpen())
    {
      const libfreenect2::RecordingWriter::Stats stats = writer.getStats();
      if (recording_depth_frames != 0 && stats.written_depth_frames >= recording_depth_frames)
        break;
      if (recording_duration_seconds != 0 && std::chrono::steady_clock::now() >= deadline)
        break;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    const bool device_stopped = device->stop();
    const bool device_closed = device->close();
    const bool recording_ok = writer.close();
    const libfreenect2::RecordingWriter::Stats stats = writer.getStats();
    if (!device_stopped || !device_closed)
    {
      std::cerr << "Warning: recording was finalized, but Kinect shutdown reported: "
                << device->getLastError() << "\n";
    }
    if (!recording_ok ||
        (recording_depth_frames != 0 && stats.written_depth_frames < recording_depth_frames) ||
        (recording_duration_seconds != 0 && stats.written_color_frames == 0 &&
         stats.written_depth_frames == 0))
    {
      std::cerr << "Recording did not complete cleanly: " << writer.getLastError() << "\n";
      return 1;
    }
    std::cout << "Recorded Kinect " << serial << " with " << stats.written_color_frames
              << " color and " << stats.written_depth_frames << " depth frames; dropped "
              << stats.dropped_frames << " frames\n";
    return 0;
  }

  const unsigned int frame_types =
      libfreenect2::Frame::Color | libfreenect2::Frame::Ir | libfreenect2::Frame::Depth;
  libfreenect2::SyncMultiFrameListener legacy_listener(frame_types);
  libfreenect2::TimestampAlignedFrameListener aligned_listener(frame_types, max_delta_ticks);
  libfreenect2::FrameListener* listener =
      timestamp_aligned ? static_cast<libfreenect2::FrameListener*>(&aligned_listener)
                        : static_cast<libfreenect2::FrameListener*>(&legacy_listener);
  const auto waitForFrames = [&](libfreenect2::FrameMap& map, int timeout_ms)
  {
    return timestamp_aligned ? aligned_listener.waitForNewFrame(map, timeout_ms)
                             : legacy_listener.waitForNewFrame(map, timeout_ms);
  };
  const auto releaseFrames = [&](libfreenect2::FrameMap& map)
  {
    if (timestamp_aligned)
      aligned_listener.release(map);
    else
      legacy_listener.release(map);
  };
  device->setColorFrameListener(listener);
  device->setIrAndDepthFrameListener(listener);
  if (!device->start())
  {
    std::cerr << "Unable to start Kinect streams\n";
    device->close();
    return 1;
  }
  firmware = device->getFirmwareVersion();
  bool calibration_profile_device_match = true;
  if (use_calibration_profile)
  {
    std::string warning;
    std::string error;
    calibration_profile_device_match = calibration_profile.serial() == serial;
    if (!calibration_profile.matchesDevice(serial, firmware, allow_device_mismatch, &warning,
                                           &error))
    {
      std::cerr << "Refusing calibration profile: " << error << "\n";
      device->stop();
      device->close();
      return 2;
    }
    if (!warning.empty())
      std::cerr << "Warning: " << warning << "\n";
  }
  if (apply_depth_correction)
  {
    correction_device_match =
        correction_profile.serial == serial && correction_profile.firmware == firmware;
    if (!correction_device_match)
    {
      std::cerr << "Warning: depth correction profile was fitted for " << correction_profile.serial
                << " / " << correction_profile.firmware << ", but the open device is " << serial
                << " / " << firmware << "\n";
      if (!allow_device_mismatch)
      {
        std::cerr << "Refusing to apply a mismatched profile without "
                     "--allow-device-mismatch\n";
        device->stop();
        device->close();
        return 2;
      }
    }
  }

  libfreenect2::FrameMap frames;
  for (int i = 0; i < 20; ++i)
  {
    const bool received = waitForFrames(frames, 10000);
    if (!received)
    {
      std::cerr << "Timed out waiting for frame set " << i;
      if (timestamp_aligned)
      {
        const libfreenect2::TimestampAlignedFrameListener::Statistics statistics =
            aligned_listener.getStatistics();
        std::cerr << " (threshold=" << max_delta_ticks
                  << " ticks, delivered=" << statistics.delivered
                  << ", dropped=" << statistics.dropped << ")";
      }
      std::cerr << "\n";
      device->stop();
      device->close();
      return 1;
    }
    if (i != 19)
    {
      releaseFrames(frames);
    }
  }

  const libfreenect2::FrameMap::iterator color_it = frames.find(libfreenect2::Frame::Color);
  const libfreenect2::FrameMap::iterator ir_it = frames.find(libfreenect2::Frame::Ir);
  const libfreenect2::FrameMap::iterator depth_it = frames.find(libfreenect2::Frame::Depth);
  libfreenect2::Frame* color = color_it == frames.end() ? 0 : color_it->second;
  libfreenect2::Frame* ir = ir_it == frames.end() ? 0 : ir_it->second;
  libfreenect2::Frame* depth = depth_it == frames.end() ? 0 : depth_it->second;
  if (!color || !ir || !depth || color->status != 0 || ir->status != 0 || depth->status != 0)
  {
    std::cerr << "Received an incomplete or invalid synchronized frame set\n";
    releaseFrames(frames);
    device->stop();
    device->close();
    return 1;
  }

  if (apply_depth_correction && !correction_profile.apply(*depth))
  {
    std::cerr << "Depth correction profile could not be applied to the decoded depth frame\n";
    releaseFrames(frames);
    device->stop();
    device->close();
    return 1;
  }

  libfreenect2::Registration registration(device->getIrCameraParams(),
                                          device->getColorCameraParams());
  libfreenect2::Frame undistorted(512, 424, 4, nullptr, libfreenect2::Frame::Float);
  libfreenect2::Frame registered(512, 424, 4, nullptr, libfreenect2::Frame::BGRX);
  registration.apply(color, depth, &undistorted, &registered);

  std::unique_ptr<libfreenect2::Frame> projective_depth;
  if (use_calibration_profile)
  {
    libfreenect2::CalibrationProfile scaled_profile = calibration_profile;
    scaled_profile.setIrCamera(
        calibration_profile.irCamera().scaledTo(depth->width, depth->height));
    const libfreenect2::ProjectiveCameraModel target =
        calibration_profile.colorCamera().scaledTo(color->width, color->height);
    scaled_profile.setColorCamera(target);
    std::string error;
    std::unique_ptr<libfreenect2::ProjectiveRegistration> projective_registration =
        libfreenect2::ProjectiveRegistration::create(
            scaled_profile, target, libfreenect2::ProjectiveRegistrationOptions(), &error);
    projective_depth = std::make_unique<libfreenect2::Frame>(
        target.width, target.height, sizeof(float), nullptr, libfreenect2::Frame::Float);
    if (!projective_registration ||
        !projective_registration->apply(*depth, *projective_depth, &error))
    {
      std::cerr << "Projective registration failed: " << error << "\n";
      releaseFrames(frames);
      device->stop();
      device->close();
      return 1;
    }
  }

  DepthStats stats;
  DepthStats projective_stats;
  bool output_ok = writeColorPpm(output + "/rgb.ppm", *color);
  output_ok = writeColorPpm(output + "/registered.ppm", registered) && output_ok;
  output_ok = writeIrPgm(output + "/infrared.pgm", *ir) && output_ok;
  output_ok =
      writeDepth(output + "/depth_mm.pgm", output + "/depth_false_color.ppm", *depth, stats) &&
      output_ok;
  if (projective_depth)
    output_ok = writeDepth(output + "/registered_depth_mm.pgm",
                           output + "/registered_depth_false_color.ppm", *projective_depth,
                           projective_stats) &&
                output_ok;

  std::ofstream metadata(output + "/metadata.json");
  const libfreenect2::TimestampAlignedFrameListener::Statistics alignment_statistics =
      aligned_listener.getStatistics();
  metadata << std::fixed << std::setprecision(2) << "{\n"
           << "  \"serial\": \"" << serial << "\",\n"
           << "  \"firmware\": \"" << firmware << "\",\n"
           << "  \"pipeline\": \"" << pipeline_name << "\",\n"
           << "  \"color_size\": [" << color->width << ", " << color->height << "],\n"
           << "  \"depth_size\": [" << depth->width << ", " << depth->height << "],\n"
           << "  \"valid_depth_pixels\": " << stats.valid << ",\n"
           << "  \"valid_depth_percent\": "
           << (100.0 * stats.valid / (depth->width * depth->height)) << ",\n"
           << "  \"minimum_depth_mm\": " << (stats.valid ? stats.minimum : 0.0f) << ",\n"
           << "  \"mean_depth_mm\": " << (stats.valid ? stats.sum / stats.valid : 0.0) << ",\n"
           << "  \"maximum_depth_mm\": " << (stats.valid ? stats.maximum : 0.0f) << ",\n"
           << "  \"capture_mode\": \""
           << (timestamp_aligned ? "timestamp_aligned" : "legacy_pairing") << "\",\n"
           << "  \"frame_timestamps\": {\n"
           << "    \"color\": {\"device_ticks\": " << color->timestamp
           << ", \"arrival_us\": " << color->arrival_timestamp_us
           << ", \"sequence\": " << color->sequence << "},\n"
           << "    \"ir\": {\"device_ticks\": " << ir->timestamp
           << ", \"arrival_us\": " << ir->arrival_timestamp_us << ", \"sequence\": " << ir->sequence
           << "},\n"
           << "    \"depth\": {\"device_ticks\": " << depth->timestamp
           << ", \"arrival_us\": " << depth->arrival_timestamp_us
           << ", \"sequence\": " << depth->sequence << "}\n"
           << "  }";
  if (apply_depth_correction)
  {
    // max_digits10 needs defaultfloat to mean "round-trippable"; under the
    // std::fixed set above it would only pad to 17 fractional digits.
    metadata << ",\n"
             << std::defaultfloat << std::setprecision(std::numeric_limits<double>::max_digits10)
             << "  \"depth_correction\": {\"scale\": " << correction_profile.scale
             << ", \"offset_mm\": " << correction_profile.offset_mm
             << ", \"rmse_mm\": " << correction_profile.rmse_mm
             << ", \"device_match\": " << (correction_device_match ? "true" : "false") << "}"
             << std::fixed << std::setprecision(2);
  }
  if (use_calibration_profile)
  {
    metadata << ",\n"
             << "  \"calibration_profile\": {\"path\": \"" << calibration_profile_path
             << "\", \"serial_match\": " << (calibration_profile_device_match ? "true" : "false")
             << ", \"registered_depth_pixels\": " << projective_stats.valid << "}";
  }
  if (timestamp_aligned)
  {
    metadata << ",\n"
             << "  \"alignment\": {\"threshold_ticks\": " << max_delta_ticks
             << ", \"delivered\": " << alignment_statistics.delivered
             << ", \"dropped\": " << alignment_statistics.dropped
             << ", \"last_delta_ticks\": " << alignment_statistics.last_delta_ticks
             << ", \"maximum_delta_ticks\": " << alignment_statistics.maximum_delta_ticks << "}";
  }
  metadata << "\n"
           << "}\n";
  metadata.flush();
  output_ok = metadata.good() && output_ok;

  releaseFrames(frames);
  device->stop();
  device->close();
  if (!output_ok)
  {
    std::cerr << "Failed to write one or more capture artifacts to " << output << "\n";
    return 1;
  }
  std::cout << "Captured Kinect " << serial << ": " << stats.valid << " valid depth pixels";
  if (apply_depth_correction)
    std::cout << "; applied depth correction from " << correction_profile_path;
  if (use_calibration_profile)
    std::cout << "; projected depth with " << calibration_profile_path;
  if (timestamp_aligned)
  {
    std::cout << "; alignment threshold=" << max_delta_ticks
              << " ticks, delivered=" << alignment_statistics.delivered
              << ", dropped=" << alignment_statistics.dropped
              << ", last delta=" << alignment_statistics.last_delta_ticks
              << ", maximum delta=" << alignment_statistics.maximum_delta_ticks;
  }
  std::cout << "\n";
  return 0;
}
