/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#include <libfreenect2/depth_calibration.h>

#include <cmath>

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

} // namespace libfreenect2
