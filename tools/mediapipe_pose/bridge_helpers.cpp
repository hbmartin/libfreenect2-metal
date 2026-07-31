#include "bridge_helpers.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mediapipe_pose
{
namespace
{

struct Candidate
{
  int index;
  float depth;
  int distance_squared;
};

void collectCandidates(int center_x, int center_y, int radius,
                       const std::vector<int32_t>& color_to_depth, size_t color_width,
                       size_t color_height, const float* depth_mm, size_t depth_pixel_count,
                       std::vector<Candidate>& candidates)
{
  candidates.clear();
  const int width = static_cast<int>(color_width);
  const int height = static_cast<int>(color_height);
  const int min_x = std::max(0, center_x - radius);
  const int max_x = std::min(width - 1, center_x + radius);
  const int min_y = std::max(0, center_y - radius);
  const int max_y = std::min(height - 1, center_y + radius);
  const int radius_squared = radius * radius;

  for (int y = min_y; y <= max_y; ++y)
  {
    for (int x = min_x; x <= max_x; ++x)
    {
      const int dx = x - center_x;
      const int dy = y - center_y;
      const int distance_squared = dx * dx + dy * dy;
      if (distance_squared > radius_squared)
        continue;

      const int32_t depth_index = color_to_depth[static_cast<size_t>(y) * color_width + x];
      if (depth_index < 0 || static_cast<size_t>(depth_index) >= depth_pixel_count)
        continue;
      const float depth = depth_mm[depth_index];
      if (!std::isfinite(depth) || depth <= 0.0f)
        continue;
      candidates.push_back(Candidate{depth_index, depth, distance_squared});
    }
  }
}

int selectCandidate(std::vector<Candidate> candidates, float cluster_span_mm, bool require_cluster)
{
  if (candidates.empty())
    return -1;

  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& left, const Candidate& right)
            {
              if (left.depth != right.depth)
                return left.depth < right.depth;
              return left.distance_squared < right.distance_squared;
            });

  size_t cluster_begin = 0;
  size_t cluster_end = 0;
  bool found_cluster = false;
  for (size_t begin = 0; begin < candidates.size(); ++begin)
  {
    size_t end = begin;
    while (end + 1 < candidates.size() &&
           candidates[end + 1].depth - candidates[begin].depth <= cluster_span_mm)
      ++end;
    if (end - begin + 1 >= 3)
    {
      cluster_begin = begin;
      cluster_end = end;
      found_cluster = true;
      break;
    }
  }

  if (!found_cluster)
    return require_cluster ? -1 : candidates.front().index;

  const Candidate* best = &candidates[cluster_begin];
  for (size_t i = cluster_begin + 1; i <= cluster_end; ++i)
  {
    if (candidates[i].distance_squared < best->distance_squared)
      best = &candidates[i];
  }
  return best->index;
}

} // namespace

bool convertColorToRgb(const unsigned char* source, size_t width, size_t height,
                       size_t bytes_per_pixel, ColorFormat format, unsigned char* destination,
                       size_t destination_size)
{
  if (!source || !destination || bytes_per_pixel < 4)
    return false;
  const size_t pixel_count = width * height;
  if (destination_size < pixel_count * 3)
    return false;

  for (size_t i = 0; i < pixel_count; ++i)
  {
    const unsigned char* input = source + i * bytes_per_pixel;
    unsigned char* output = destination + i * 3;
    if (format == ColorFormatRgbx)
    {
      output[0] = input[0];
      output[1] = input[1];
      output[2] = input[2];
    }
    else
    {
      output[0] = input[2];
      output[1] = input[1];
      output[2] = input[0];
    }
  }
  return true;
}

void buildReverseColorMap(const float* depth_mm, size_t depth_pixel_count,
                          const int* depth_to_color, size_t color_pixel_count,
                          std::vector<int32_t>& color_to_depth)
{
  color_to_depth.assign(color_pixel_count, -1);
  if (!depth_mm || !depth_to_color)
    return;

  for (size_t depth_index = 0; depth_index < depth_pixel_count; ++depth_index)
  {
    const int color_index = depth_to_color[depth_index];
    const float depth = depth_mm[depth_index];
    if (color_index < 0 || static_cast<size_t>(color_index) >= color_pixel_count ||
        !std::isfinite(depth) || depth <= 0.0f)
      continue;

    int32_t& existing = color_to_depth[color_index];
    if (existing < 0 || depth < depth_mm[existing])
      existing = static_cast<int32_t>(depth_index);
  }
}

int findDepthPixel(float normalized_x, float normalized_y,
                   const std::vector<int32_t>& color_to_depth, size_t color_width,
                   size_t color_height, const float* depth_mm, size_t depth_pixel_count,
                   int primary_radius, int fallback_radius, float cluster_span_mm)
{
  if (!std::isfinite(normalized_x) || !std::isfinite(normalized_y) || normalized_x < 0.0f ||
      normalized_x > 1.0f || normalized_y < 0.0f || normalized_y > 1.0f || color_width == 0 ||
      color_height == 0 || !depth_mm || color_to_depth.size() != color_width * color_height ||
      primary_radius < 0 || fallback_radius < primary_radius || cluster_span_mm < 0.0f)
    return -1;

  const int center_x = static_cast<int>(std::lround(normalized_x * (color_width - 1)));
  const int center_y = static_cast<int>(std::lround(normalized_y * (color_height - 1)));
  std::vector<Candidate> primary;
  collectCandidates(center_x, center_y, primary_radius, color_to_depth, color_width, color_height,
                    depth_mm, depth_pixel_count, primary);
  int selected = selectCandidate(primary, cluster_span_mm, true);
  if (selected >= 0)
    return selected;

  std::vector<Candidate> fallback;
  collectCandidates(center_x, center_y, fallback_radius, color_to_depth, color_width, color_height,
                    depth_mm, depth_pixel_count, fallback);
  selected = selectCandidate(fallback, cluster_span_mm, true);
  if (selected >= 0)
    return selected;

  if (!primary.empty())
    return selectCandidate(primary, cluster_span_mm, false);
  return selectCandidate(fallback, cluster_span_mm, false);
}

float timestampDeltaMilliseconds(uint32_t first, uint32_t second)
{
  const int32_t delta_ticks = static_cast<int32_t>(first - second);
  return std::fabs(static_cast<float>(delta_ticks)) * 0.125f;
}

} // namespace mediapipe_pose
