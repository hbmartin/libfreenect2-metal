#ifndef LIBFREENECT2_MEDIAPIPE_POSE_BRIDGE_HELPERS_H_
#define LIBFREENECT2_MEDIAPIPE_POSE_BRIDGE_HELPERS_H_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mediapipe_pose
{

enum ColorFormat
{
  ColorFormatBgrx,
  ColorFormatRgbx
};

bool convertColorToRgb(const unsigned char* source, size_t width, size_t height,
                       size_t bytes_per_pixel, ColorFormat format, unsigned char* destination,
                       size_t destination_size);

void buildReverseColorMap(const float* depth_mm, size_t depth_pixel_count,
                          const int* depth_to_color, size_t color_pixel_count,
                          std::vector<int32_t>& color_to_depth);

int findDepthPixel(float normalized_x, float normalized_y,
                   const std::vector<int32_t>& color_to_depth, size_t color_width,
                   size_t color_height, const float* depth_mm, size_t depth_pixel_count,
                   int primary_radius, int fallback_radius, float cluster_span_mm);

float timestampDeltaMilliseconds(uint32_t first, uint32_t second);

} // namespace mediapipe_pose

#endif
