/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#include <libfreenect2/depth_calibration.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace libfreenect2
{

DepthCorrectionProfile::DepthCorrectionProfile() : scale(1.0), offset_mm(0.0)
{
}

bool DepthCorrectionProfile::isValid() const
{
  return std::isfinite(scale) && scale > 0.0 && std::isfinite(offset_mm);
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

DepthCalibrationRoi::DepthCalibrationRoi() : x(0), y(0), width(0), height(0)
{
}

DepthCalibrationRoi::DepthCalibrationRoi(uint32_t x_arg, uint32_t y_arg, uint32_t width_arg,
                                         uint32_t height_arg)
    : x(x_arg), y(y_arg), width(width_arg), height(height_arg)
{
}

DepthRoiStatistics::DepthRoiStatistics() : median_mm(0.0), mad_mm(0.0), valid_pixel_count(0)
{
}

namespace
{

double median(std::vector<double>& values)
{
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

} // namespace

bool computeDepthRoiStatistics(const Frame& depth, const DepthCalibrationRoi& roi,
                               DepthRoiStatistics& statistics, std::string* error)
{
  statistics = DepthRoiStatistics();
  if (depth.data == NULL || depth.format != Frame::Float ||
      depth.bytes_per_pixel != sizeof(float))
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

} // namespace libfreenect2
