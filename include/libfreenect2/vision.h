/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * Copyright (c) 2026 individual OpenKinect contributors.
 *
 * This code is licensed under the terms described in the LICENSES directory.
 */

/** @file vision.h Reusable color conversion and depth geometry helpers. */

#ifndef LIBFREENECT2_VISION_H_
#define LIBFREENECT2_VISION_H_

#include <cstddef>
#include <stdint.h>

#include <libfreenect2/config.h>
#include <libfreenect2/frame_listener.hpp>
#include <libfreenect2/registration.h>

namespace libfreenect2
{
namespace vision
{

/** Channel order for three-channel color output. */
enum ColorOrder
{
  BGR = 0,
  RGB = 1
};

/** Options for locating a coherent foreground depth sample near a color point. */
struct LIBFREENECT2_API DepthSearchOptions
{
  int primary_radius;     ///< First search radius in color pixels.
  int fallback_radius;    ///< Expanded search radius in color pixels.
  float cluster_span_mm;  ///< Maximum depth span of a coherent cluster.

  DepthSearchOptions();
};

/** Convert a BGRX or RGBX frame to contiguous three-channel BGR or RGB.
 * @return false when an argument, frame format, size, or destination capacity is invalid.
 */
LIBFREENECT2_API bool convertColorFrame(const Frame* source, ColorOrder order,
                                        unsigned char* destination,
                                        size_t destination_size);

/** Reverse a depth-to-color index map, keeping the nearest valid depth on collisions.
 * Missing color pixels are written as -1.
 */
LIBFREENECT2_API bool buildColorToDepthMap(const Frame* undistorted_depth,
                                           const int* depth_to_color,
                                           size_t depth_to_color_count,
                                           int32_t* color_to_depth,
                                           size_t color_pixel_count);

/** Find the depth pixel corresponding to normalized color coordinates.
 * @return a linear depth index, or -1 when no valid sample can be selected.
 */
LIBFREENECT2_API int findDepthPixel(float normalized_x, float normalized_y,
                                    const int32_t* color_to_depth,
                                    size_t color_pixel_count, size_t color_width,
                                    size_t color_height,
                                    const Frame* undistorted_depth,
                                    const DepthSearchOptions& options);

/** Lift normalized color coordinates to metric XYZ points.
 *
 * `normalized_xy` contains interleaved x/y pairs. `xyz_meters`, `valid`, and
 * `depth_indices` must have capacity for `landmark_count * 3`,
 * `landmark_count`, and `landmark_count` elements respectively. Individual
 * points without a valid depth sample receive NaN XYZ, validity 0, and index
 * -1. The function returns false only for invalid overall arguments.
 */
LIBFREENECT2_API bool liftColorPoints(const Registration* registration,
                                      const Frame* undistorted_depth,
                                      const int32_t* color_to_depth,
                                      size_t color_pixel_count,
                                      size_t color_width, size_t color_height,
                                      const float* normalized_xy,
                                      size_t landmark_count,
                                      const DepthSearchOptions& options,
                                      float* xyz_meters, uint8_t* valid,
                                      int32_t* depth_indices);

} // namespace vision
} // namespace libfreenect2

#endif // LIBFREENECT2_VISION_H_
