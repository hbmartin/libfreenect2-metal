/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#ifndef LIBFREENECT2_DEPTH_CALIBRATION_H_
#define LIBFREENECT2_DEPTH_CALIBRATION_H_

#include <libfreenect2/frame_listener.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace libfreenect2
{

/** Opt-in linear correction for decoded depth measurements in millimeters. */
struct LIBFREENECT2_API DepthCorrectionProfile
{
  DepthCorrectionProfile();

  double scale;
  double offset_mm;

  /** A profile is usable only when its coefficients are finite and scale is positive. */
  bool isValid() const;

  /** Return `scale * measured_mm + offset_mm`, preserving invalid measurements. */
  float correct(float measured_mm) const;

  /** Apply this profile to a decoded float depth frame. Invalid pixels are unchanged. */
  bool apply(Frame& depth) const;
};

/** Pixel rectangle used to measure a planar calibration target. */
struct LIBFREENECT2_API DepthCalibrationRoi
{
  DepthCalibrationRoi();
  DepthCalibrationRoi(uint32_t x_arg, uint32_t y_arg, uint32_t width_arg,
                      uint32_t height_arg);

  uint32_t x;
  uint32_t y;
  uint32_t width;
  uint32_t height;
};

/** Robust statistics for valid depth pixels in one frame and ROI. */
struct LIBFREENECT2_API DepthRoiStatistics
{
  DepthRoiStatistics();

  double median_mm;
  double mad_mm;
  size_t valid_pixel_count;
};

/** Compute the median and median absolute deviation for one decoded depth frame. */
LIBFREENECT2_API bool computeDepthRoiStatistics(const Frame& depth,
                                                const DepthCalibrationRoi& roi,
                                                DepthRoiStatistics& statistics,
                                                std::string* error = 0);

} // namespace libfreenect2

#endif // LIBFREENECT2_DEPTH_CALIBRATION_H_
