/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#include <libfreenect2/depth_calibration.h>
#include <libfreenect2/recording_utils.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <vector>

namespace libfreenect2
{

DepthCorrectionProfile::DepthCorrectionProfile()
    : version(1), model(OffsetOnly), scale(1.0), offset_mm(0.0), rmse_mm(0.0)
{
}

bool DepthCorrectionProfile::isValid() const
{
  return version == 1 && (model == OffsetOnly || model == Linear) && std::isfinite(scale) &&
         scale > 0.0 && std::isfinite(offset_mm) && std::isfinite(rmse_mm) && rmse_mm >= 0.0 &&
         samples.size() == residuals_mm.size();
}

float DepthCorrectionProfile::correct(float measured_mm) const
{
  if (!isValid() || !std::isfinite(measured_mm) || measured_mm <= 0.0f)
    return measured_mm;
  return static_cast<float>(scale * measured_mm + offset_mm);
}

bool DepthCorrectionProfile::apply(Frame& depth) const
{
  if (!isValid() || depth.data == NULL || depth.format != Frame::Float ||
      depth.bytes_per_pixel != sizeof(float))
    return false;

  float* pixels = reinterpret_cast<float*>(depth.data);
  const size_t pixel_count = depth.width * depth.height;
  for (size_t index = 0; index < pixel_count; ++index)
  {
    if (std::isfinite(pixels[index]) && pixels[index] > 0.0f)
      pixels[index] = static_cast<float>(scale * pixels[index] + offset_mm);
  }
  return true;
}

DepthCalibrationRoi::DepthCalibrationRoi() : x(0), y(0), width(0), height(0) {}

DepthCalibrationRoi::DepthCalibrationRoi(uint32_t x_arg, uint32_t y_arg, uint32_t width_arg,
                                         uint32_t height_arg)
    : x(x_arg), y(y_arg), width(width_arg), height(height_arg)
{
}

DepthRoiStatistics::DepthRoiStatistics() : median_mm(0.0), mad_mm(0.0), valid_pixel_count(0) {}

DepthCalibrationSample::DepthCalibrationSample()
    : known_distance_mm(0.0), measured_median_mm(0.0), mad_mm(0.0), valid_pixel_count(0)
{
}

namespace
{

double median(std::vector<double>& values)
{
  if (values.empty())
    return 0.0;
  std::sort(values.begin(), values.end());
  const size_t middle = values.size() / 2;
  if (values.size() % 2 != 0)
    return values[middle];
  return (values[middle - 1] + values[middle]) * 0.5;
}

bool fail(const std::string& message, std::string* error)
{
  if (error != 0)
    *error = message;
  return false;
}

bool validateSerializableProfile(const DepthCorrectionProfile& profile, std::string* error)
{
  if (!profile.isValid())
    return fail("depth correction profile has invalid coefficients or diagnostics", error);
  if (profile.serial.empty() || profile.firmware.empty())
    return fail("depth correction profile requires device serial and firmware", error);
  if (profile.roi.width == 0 || profile.roi.height == 0)
    return fail("depth correction profile requires a non-empty ROI", error);
  if (profile.samples.empty())
    return fail("depth correction profile requires calibration samples", error);
  for (size_t index = 0; index < profile.samples.size(); ++index)
  {
    const DepthCalibrationSample& sample = profile.samples[index];
    if (!std::isfinite(sample.known_distance_mm) || sample.known_distance_mm <= 0.0 ||
        !std::isfinite(sample.measured_median_mm) || sample.measured_median_mm <= 0.0 ||
        !std::isfinite(sample.mad_mm) || sample.mad_mm < 0.0 || sample.valid_pixel_count == 0 ||
        !std::isfinite(profile.residuals_mm[index]))
      return fail("depth correction profile contains an invalid sample", error);
  }
  return true;
}

} // namespace

bool computeDepthRoiStatistics(const Frame& depth, const DepthCalibrationRoi& roi,
                               DepthRoiStatistics& statistics, std::string* error)
{
  statistics = DepthRoiStatistics();
  if (depth.data == NULL || depth.format != Frame::Float || depth.bytes_per_pixel != sizeof(float))
    return fail("depth frame is not decoded float data", error);
  if (roi.width == 0 || roi.height == 0 || roi.x >= depth.width || roi.y >= depth.height ||
      roi.width > depth.width - roi.x || roi.height > depth.height - roi.y)
    return fail("depth calibration ROI is outside the frame", error);

  const float* pixels = reinterpret_cast<const float*>(depth.data);
  std::vector<double> valid;
  valid.reserve(static_cast<size_t>(roi.width) * roi.height);
  for (size_t row = roi.y; row < static_cast<size_t>(roi.y) + roi.height; ++row)
  {
    const size_t row_offset = row * depth.width;
    for (size_t column = roi.x; column < static_cast<size_t>(roi.x) + roi.width; ++column)
    {
      const float measured_mm = pixels[row_offset + column];
      if (std::isfinite(measured_mm) && measured_mm > 0.0f)
        valid.push_back(measured_mm);
    }
  }
  if (valid.empty())
    return fail("depth calibration ROI contains no valid pixels", error);

  statistics.valid_pixel_count = valid.size();
  statistics.median_mm = median(valid);
  for (size_t index = 0; index < valid.size(); ++index)
    valid[index] = std::fabs(valid[index] - statistics.median_mm);
  statistics.mad_mm = median(valid);
  if (error != 0)
    error->clear();
  return true;
}

bool fitDepthCorrectionProfile(const std::vector<DepthCalibrationSample>& samples,
                               DepthCorrectionProfile& profile, std::string* error)
{
  if (samples.empty())
    return fail("at least one calibration sample is required", error);

  std::map<double, std::vector<double>> measured_by_distance;
  for (size_t index = 0; index < samples.size(); ++index)
  {
    const DepthCalibrationSample& sample = samples[index];
    if (!std::isfinite(sample.known_distance_mm) || sample.known_distance_mm <= 0.0 ||
        !std::isfinite(sample.measured_median_mm) || sample.measured_median_mm <= 0.0 ||
        !std::isfinite(sample.mad_mm) || sample.mad_mm < 0.0 || sample.valid_pixel_count == 0)
      return fail("calibration samples must contain finite positive distances and valid pixels",
                  error);
    measured_by_distance[sample.known_distance_mm].push_back(sample.measured_median_mm);
  }

  std::vector<double> known_distances;
  std::vector<double> measured_medians;
  for (std::map<double, std::vector<double>>::iterator group = measured_by_distance.begin();
       group != measured_by_distance.end(); ++group)
  {
    known_distances.push_back(group->first);
    measured_medians.push_back(median(group->second));
  }

  DepthCorrectionProfile fitted;
  if (known_distances.size() == 1)
  {
    fitted.model = DepthCorrectionProfile::OffsetOnly;
    fitted.scale = 1.0;
    fitted.offset_mm = known_distances[0] - measured_medians[0];
  }
  else
  {
    double measured_mean = 0.0;
    double known_mean = 0.0;
    for (size_t index = 0; index < known_distances.size(); ++index)
    {
      measured_mean += measured_medians[index];
      known_mean += known_distances[index];
    }
    measured_mean /= measured_medians.size();
    known_mean /= known_distances.size();

    double covariance = 0.0;
    double variance = 0.0;
    for (size_t index = 0; index < known_distances.size(); ++index)
    {
      const double measured_delta = measured_medians[index] - measured_mean;
      covariance += measured_delta * (known_distances[index] - known_mean);
      variance += measured_delta * measured_delta;
    }
    if (!std::isfinite(variance) || variance <= 0.0)
      return fail("distinct known distances produced no measurable depth range", error);

    fitted.model = DepthCorrectionProfile::Linear;
    fitted.scale = covariance / variance;
    fitted.offset_mm = known_mean - fitted.scale * measured_mean;
    if (!fitted.isValid())
      return fail("calibration fit produced invalid correction coefficients", error);
  }

  fitted.samples = samples;
  fitted.residuals_mm.reserve(samples.size());
  double squared_error_sum = 0.0;
  for (size_t index = 0; index < samples.size(); ++index)
  {
    const double residual = fitted.scale * samples[index].measured_median_mm + fitted.offset_mm -
                            samples[index].known_distance_mm;
    fitted.residuals_mm.push_back(residual);
    squared_error_sum += residual * residual;
  }
  fitted.rmse_mm = std::sqrt(squared_error_sum / samples.size());
  if (!fitted.isValid() || !std::isfinite(fitted.rmse_mm))
    return fail("calibration fit produced non-finite diagnostics", error);

  profile = fitted;
  if (error != 0)
    error->clear();
  return true;
}

bool DepthCorrectionProfile::save(const std::string& path, std::string* error) const
{
  if (path.empty())
    return fail("depth correction profile path is empty", error);
  if (!validateSerializableProfile(*this, error))
    return false;

  nlohmann::json root;
  root["format"] = "libfreenect2-depth-correction";
  root["version"] = version;
  root["device"] = {{"serial", serial}, {"firmware", firmware}};
  root["model"] = model == OffsetOnly ? "offset_only" : "linear";
  root["formula"] = "corrected_mm = scale * measured_mm + offset_mm";
  root["scale"] = scale;
  root["offset_mm"] = offset_mm;
  root["rmse_mm"] = rmse_mm;
  root["roi"] = {{"x", roi.x}, {"y", roi.y}, {"width", roi.width}, {"height", roi.height}};
  root["samples"] = nlohmann::json::array();
  for (size_t index = 0; index < samples.size(); ++index)
  {
    root["samples"].push_back({{"known_distance_mm", samples[index].known_distance_mm},
                               {"measured_median_mm", samples[index].measured_median_mm},
                               {"mad_mm", samples[index].mad_mm},
                               {"valid_pixel_count", samples[index].valid_pixel_count},
                               {"residual_mm", residuals_mm[index]}});
  }
  return recording::writeFileAtomically(path, root.dump(2) + "\n", error);
}

bool DepthCorrectionProfile::load(const std::string& path, DepthCorrectionProfile& profile,
                                  std::string* error)
{
  std::vector<unsigned char> bytes;
  if (!recording::readFile(path, bytes, error))
    return false;

  try
  {
    const nlohmann::json root = nlohmann::json::parse(bytes.begin(), bytes.end());
    if (root.at("format").get<std::string>() != "libfreenect2-depth-correction" ||
        root.at("version").get<uint32_t>() != 1)
      return fail("unsupported depth correction profile version", error);

    DepthCorrectionProfile loaded;
    loaded.version = root.at("version").get<uint32_t>();
    loaded.serial = root.at("device").at("serial").get<std::string>();
    loaded.firmware = root.at("device").at("firmware").get<std::string>();
    const std::string model_name = root.at("model").get<std::string>();
    if (model_name == "offset_only")
      loaded.model = OffsetOnly;
    else if (model_name == "linear")
      loaded.model = Linear;
    else
      return fail("unsupported depth correction model", error);
    if (root.at("formula").get<std::string>() != "corrected_mm = scale * measured_mm + offset_mm")
      return fail("unsupported depth correction formula", error);
    loaded.scale = root.at("scale").get<double>();
    loaded.offset_mm = root.at("offset_mm").get<double>();
    loaded.rmse_mm = root.at("rmse_mm").get<double>();
    loaded.roi.x = root.at("roi").at("x").get<uint32_t>();
    loaded.roi.y = root.at("roi").at("y").get<uint32_t>();
    loaded.roi.width = root.at("roi").at("width").get<uint32_t>();
    loaded.roi.height = root.at("roi").at("height").get<uint32_t>();

    const nlohmann::json& serialized_samples = root.at("samples");
    if (!serialized_samples.is_array())
      return fail("depth correction samples must be an array", error);
    for (nlohmann::json::const_iterator item = serialized_samples.begin();
         item != serialized_samples.end(); ++item)
    {
      DepthCalibrationSample sample;
      sample.known_distance_mm = item->at("known_distance_mm").get<double>();
      sample.measured_median_mm = item->at("measured_median_mm").get<double>();
      sample.mad_mm = item->at("mad_mm").get<double>();
      const uint64_t valid_pixel_count = item->at("valid_pixel_count").get<uint64_t>();
      if (sizeof(size_t) < sizeof(uint64_t) &&
          valid_pixel_count > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
        return fail("depth correction sample count is too large", error);
      sample.valid_pixel_count = static_cast<size_t>(valid_pixel_count);
      loaded.samples.push_back(sample);
      loaded.residuals_mm.push_back(item->at("residual_mm").get<double>());
    }
    if (!validateSerializableProfile(loaded, error))
      return false;
    profile = loaded;
    if (error != 0)
      error->clear();
    return true;
  }
  catch (const std::exception& exception)
  {
    return fail(std::string("invalid depth correction profile: ") + exception.what(), error);
  }
}

} // namespace libfreenect2
