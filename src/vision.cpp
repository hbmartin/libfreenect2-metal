#include <libfreenect2/vision.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace libfreenect2
{
namespace vision
{
namespace
{

struct Candidate
{
  int index;
  float depth;
  int distance_squared;
};

bool multiplyOverflows(size_t left, size_t right)
{
  return right != 0 && left > std::numeric_limits<size_t>::max() / right;
}

bool validDepthFrame(const Frame* frame)
{
  if (!frame || !frame->data || frame->format != Frame::Float ||
      frame->bytes_per_pixel != sizeof(float) || frame->width == 0 || frame->height == 0 ||
      multiplyOverflows(frame->width, frame->height) ||
      multiplyOverflows(frame->width * frame->height, sizeof(float)))
    return false;
  return true;
}

bool validSearchArguments(const int32_t* color_to_depth, size_t color_pixel_count,
                          size_t color_width, size_t color_height,
                          const Frame* undistorted_depth, const DepthSearchOptions& options)
{
  if (!color_to_depth || !validDepthFrame(undistorted_depth) || color_width == 0 ||
      color_height == 0 || multiplyOverflows(color_width, color_height) ||
      color_width * color_height != color_pixel_count ||
      color_width > static_cast<size_t>(std::numeric_limits<int>::max()) ||
      color_height > static_cast<size_t>(std::numeric_limits<int>::max()) ||
      multiplyOverflows(undistorted_depth->width, undistorted_depth->height) ||
      undistorted_depth->width * undistorted_depth->height >
          static_cast<size_t>(std::numeric_limits<int32_t>::max()) ||
      options.primary_radius < 0 || options.fallback_radius < options.primary_radius ||
      !std::isfinite(options.cluster_span_mm) || options.cluster_span_mm < 0.0f ||
      static_cast<size_t>(options.fallback_radius) > std::max(color_width, color_height))
    return false;
  return true;
}

bool byDepthThenDistance(const Candidate& left, const Candidate& right)
{
  if (left.depth != right.depth)
    return left.depth < right.depth;
  return left.distance_squared < right.distance_squared;
}

void collectCandidates(int center_x, int center_y, int radius,
                       const int32_t* color_to_depth, size_t color_width,
                       size_t color_height, const Frame* undistorted_depth,
                       std::vector<Candidate>& candidates)
{
  candidates.clear();
  const int width = static_cast<int>(color_width);
  const int height = static_cast<int>(color_height);
  const int min_x = static_cast<int>(std::max<int64_t>(0, static_cast<int64_t>(center_x) - radius));
  const int max_x = static_cast<int>(std::min<int64_t>(width - 1, static_cast<int64_t>(center_x) + radius));
  const int min_y = static_cast<int>(std::max<int64_t>(0, static_cast<int64_t>(center_y) - radius));
  const int max_y = static_cast<int>(std::min<int64_t>(height - 1, static_cast<int64_t>(center_y) + radius));
  const int64_t radius_squared = static_cast<int64_t>(radius) * radius;
  const float* depth_mm = reinterpret_cast<const float*>(undistorted_depth->data);
  const size_t depth_pixel_count = undistorted_depth->width * undistorted_depth->height;

  for (int y = min_y; y <= max_y; ++y)
  {
    for (int x = min_x; x <= max_x; ++x)
    {
      const int dx = x - center_x;
      const int dy = y - center_y;
      const int64_t distance_squared_wide = static_cast<int64_t>(dx) * dx + static_cast<int64_t>(dy) * dy;
      if (distance_squared_wide > radius_squared)
        continue;
      const int distance_squared = distance_squared_wide > std::numeric_limits<int>::max()
                                       ? std::numeric_limits<int>::max()
                                       : static_cast<int>(distance_squared_wide);

      const int32_t depth_index = color_to_depth[static_cast<size_t>(y) * color_width + x];
      if (depth_index < 0 || static_cast<size_t>(depth_index) >= depth_pixel_count)
        continue;
      const float depth = depth_mm[depth_index];
      if (!std::isfinite(depth) || depth <= 0.0f)
        continue;
      candidates.push_back(Candidate{depth_index, depth, distance_squared});
    }
  }
  std::sort(candidates.begin(), candidates.end(), byDepthThenDistance);
}

int selectCandidate(const std::vector<Candidate>& candidates, float cluster_span_mm,
                    bool require_cluster)
{
  if (candidates.empty())
    return -1;

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

/** Search for a depth pixel assuming validSearchArguments() already passed.
 * The scratch vectors are cleared by collectCandidates() and exist so batch
 * callers can reuse their capacity across points.
 */
int searchDepthPixel(float normalized_x, float normalized_y, const int32_t* color_to_depth,
                     size_t color_width, size_t color_height, const Frame* undistorted_depth,
                     const DepthSearchOptions& options, std::vector<Candidate>& primary,
                     std::vector<Candidate>& fallback)
{
  if (!std::isfinite(normalized_x) || !std::isfinite(normalized_y) || normalized_x < 0.0f ||
      normalized_x > 1.0f || normalized_y < 0.0f || normalized_y > 1.0f)
    return -1;

  const int center_x = static_cast<int>(std::lround(normalized_x * (color_width - 1)));
  const int center_y = static_cast<int>(std::lround(normalized_y * (color_height - 1)));
  collectCandidates(center_x, center_y, options.primary_radius, color_to_depth, color_width,
                    color_height, undistorted_depth, primary);
  int selected = selectCandidate(primary, options.cluster_span_mm, true);
  if (selected >= 0)
    return selected;

  collectCandidates(center_x, center_y, options.fallback_radius, color_to_depth, color_width,
                    color_height, undistorted_depth, fallback);
  selected = selectCandidate(fallback, options.cluster_span_mm, true);
  if (selected >= 0)
    return selected;
  if (!primary.empty())
    return selectCandidate(primary, options.cluster_span_mm, false);
  return selectCandidate(fallback, options.cluster_span_mm, false);
}

} // namespace

DepthSearchOptions::DepthSearchOptions()
  : primary_radius(8), fallback_radius(20), cluster_span_mm(150.0f)
{
}

bool convertColorFrame(const Frame* source, ColorOrder order, unsigned char* destination,
                       size_t destination_size)
{
  if (!source || !source->data || !destination || source->bytes_per_pixel != 4 ||
      (source->format != Frame::BGRX && source->format != Frame::RGBX) ||
      (order != BGR && order != RGB) || multiplyOverflows(source->width, source->height))
    return false;

  const size_t pixel_count = source->width * source->height;
  if (multiplyOverflows(pixel_count, static_cast<size_t>(4)) ||
      multiplyOverflows(pixel_count, static_cast<size_t>(3)) ||
      destination_size < pixel_count * 3)
    return false;

  const bool source_is_rgb = source->format == Frame::RGBX;
  const bool output_is_rgb = order == RGB;
  for (size_t i = 0; i < pixel_count; ++i)
  {
    const unsigned char* input = source->data + i * 4;
    unsigned char* output = destination + i * 3;
    if (source_is_rgb == output_is_rgb)
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

bool buildColorToDepthMap(const Frame* undistorted_depth, const int* depth_to_color,
                          size_t depth_to_color_count, int32_t* color_to_depth,
                          size_t color_pixel_count)
{
  if (!validDepthFrame(undistorted_depth) || !depth_to_color || !color_to_depth ||
      multiplyOverflows(undistorted_depth->width, undistorted_depth->height) ||
      depth_to_color_count != undistorted_depth->width * undistorted_depth->height ||
      depth_to_color_count > static_cast<size_t>(std::numeric_limits<int32_t>::max()))
    return false;

  std::fill(color_to_depth, color_to_depth + color_pixel_count, static_cast<int32_t>(-1));
  const float* depth_mm = reinterpret_cast<const float*>(undistorted_depth->data);
  for (size_t depth_index = 0; depth_index < depth_to_color_count; ++depth_index)
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
  return true;
}

int findDepthPixel(float normalized_x, float normalized_y, const int32_t* color_to_depth,
                   size_t color_pixel_count, size_t color_width, size_t color_height,
                   const Frame* undistorted_depth, const DepthSearchOptions& options)
{
  if (!validSearchArguments(color_to_depth, color_pixel_count, color_width, color_height,
                            undistorted_depth, options))
    return -1;

  std::vector<Candidate> primary;
  std::vector<Candidate> fallback;
  return searchDepthPixel(normalized_x, normalized_y, color_to_depth, color_width, color_height,
                          undistorted_depth, options, primary, fallback);
}

bool liftColorPoints(const Registration* registration, const Frame* undistorted_depth,
                     const int32_t* color_to_depth, size_t color_pixel_count,
                     size_t color_width, size_t color_height, const float* normalized_xy,
                     size_t landmark_count, const DepthSearchOptions& options,
                     float* xyz_meters, uint8_t* valid, int32_t* depth_indices)
{
  if (!registration || !normalized_xy || !xyz_meters || !valid || !depth_indices ||
      undistorted_depth == NULL || undistorted_depth->width != 512 ||
      undistorted_depth->height != 424 || multiplyOverflows(landmark_count, 2) ||
      multiplyOverflows(landmark_count, 3) ||
      !validSearchArguments(color_to_depth, color_pixel_count, color_width, color_height,
                            undistorted_depth, options))
    return false;

  const float nan = std::numeric_limits<float>::quiet_NaN();
  const int depth_width = static_cast<int>(undistorted_depth->width);
  std::vector<Candidate> primary;
  std::vector<Candidate> fallback;
  for (size_t i = 0; i < landmark_count; ++i)
  {
    const int depth_index =
        searchDepthPixel(normalized_xy[i * 2], normalized_xy[i * 2 + 1], color_to_depth,
                         color_width, color_height, undistorted_depth, options, primary, fallback);
    depth_indices[i] = depth_index;
    valid[i] = 0;
    xyz_meters[i * 3] = nan;
    xyz_meters[i * 3 + 1] = nan;
    xyz_meters[i * 3 + 2] = nan;
    if (depth_index < 0)
      continue;

    float x = nan;
    float y = nan;
    float z = nan;
    registration->getPointXYZ(undistorted_depth, depth_index / depth_width,
                              depth_index % depth_width, x, y, z);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
    {
      depth_indices[i] = -1;
      continue;
    }
    xyz_meters[i * 3] = x;
    xyz_meters[i * 3 + 1] = y;
    xyz_meters[i * 3 + 2] = z;
    valid[i] = 1;
  }
  return true;
}

} // namespace vision
} // namespace libfreenect2
