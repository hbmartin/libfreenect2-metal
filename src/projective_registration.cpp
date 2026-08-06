/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#include <libfreenect2/projective_registration.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <utility>
#include <vector>

namespace libfreenect2
{
namespace
{

bool fail(const std::string& message, std::string* error)
{
  if (error != nullptr)
    *error = message;
  return false;
}

void distort(const ProjectiveCameraModel& camera, double x, double y, double& distorted_x,
             double& distorted_y)
{
  if (camera.distortion_model == DistortionModel::None)
  {
    distorted_x = x;
    distorted_y = y;
    return;
  }

  const double r2 = x * x + y * y;
  const double r4 = r2 * r2;
  const double r6 = r4 * r2;
  const double k1 = camera.distortion[0];
  const double k2 = camera.distortion[1];
  const double p1 = camera.distortion[2];
  const double p2 = camera.distortion[3];
  const double k3 = camera.distortion[4];
  const double denominator =
      camera.distortion_model == DistortionModel::Rational8
          ? 1.0 + camera.distortion[5] * r2 + camera.distortion[6] * r4 + camera.distortion[7] * r6
          : 1.0;
  const double radial = (1.0 + k1 * r2 + k2 * r4 + k3 * r6) / denominator;
  distorted_x = x * radial + 2.0 * p1 * x * y + p2 * (r2 + 2.0 * x * x);
  distorted_y = y * radial + p1 * (r2 + 2.0 * y * y) + 2.0 * p2 * x * y;
}

bool validDepth(float depth)
{
  return std::isfinite(depth) && depth > 0.0f;
}

bool undistort(const ProjectiveCameraModel& camera, double distorted_x, double distorted_y,
               double& x, double& y)
{
  x = distorted_x;
  y = distorted_y;
  for (size_t iteration = 0; iteration < 12; ++iteration)
  {
    double estimate_x = 0.0;
    double estimate_y = 0.0;
    distort(camera, x, y, estimate_x, estimate_y);
    const double delta_x = distorted_x - estimate_x;
    const double delta_y = distorted_y - estimate_y;
    x += delta_x;
    y += delta_y;
    if (!std::isfinite(x) || !std::isfinite(y))
      return false;
    if (delta_x * delta_x + delta_y * delta_y < 1e-20)
      break;
  }
  return true;
}

} // namespace

ProjectiveRegistrationOptions::ProjectiveRegistrationOptions()
    : rasterization(RegistrationRasterization::FourNeighborSplat), min_depth_mm(500.0f),
      max_depth_mm(4500.0f), apply_depth_correction(false)
{
}

bool ProjectiveRegistrationOptions::isValid(std::string* error) const
{
  if (rasterization != RegistrationRasterization::Nearest &&
      rasterization != RegistrationRasterization::FourNeighborSplat)
    return fail("projective registration rasterization mode is invalid", error);
  if (!std::isfinite(min_depth_mm) || !std::isfinite(max_depth_mm) || min_depth_mm <= 0.0f ||
      max_depth_mm <= min_depth_mm)
    return fail("projective registration depth range is invalid", error);
  if (error != nullptr)
    error->clear();
  return true;
}

class ProjectiveRegistrationImpl
{
public:
  CalibrationProfile profile;
  ProjectiveCameraModel source;
  ProjectiveCameraModel target;
  RigidTransform transform;
  ProjectiveRegistrationOptions options;
  std::vector<double> ray_x;
  std::vector<double> ray_y;

  bool initialize(std::string* error)
  {
    if (!profile.isValid(error) || !target.isValid(error) || !options.isValid(error))
      return false;
    if (options.apply_depth_correction && !profile.hasDepthCorrection())
      return fail("depth correction was requested but the profile has none", error);

    source = profile.irCamera();
    transform = profile.depthToColor();
    const size_t pixel_count = static_cast<size_t>(source.width) * source.height;
    ray_x.resize(pixel_count);
    ray_y.resize(pixel_count);

    for (uint32_t row = 0; row < source.height; ++row)
    {
      for (uint32_t column = 0; column < source.width; ++column)
      {
        const size_t index = static_cast<size_t>(row) * source.width + column;
        const double distorted_x = (static_cast<double>(column) - source.cx) / source.fx;
        const double distorted_y = (static_cast<double>(row) - source.cy) / source.fy;
        if (!undistort(source, distorted_x, distorted_y, ray_x[index], ray_y[index]))
          return fail("source camera distortion could not be inverted", error);
      }
    }
    if (error != nullptr)
      error->clear();
    return true;
  }

  bool apply(const Frame& depth, Frame& registered, std::string* error) const
  {
    if (depth.data == nullptr || depth.format != Frame::Float ||
        depth.bytes_per_pixel != sizeof(float) || depth.width != source.width ||
        depth.height != source.height)
      return fail("projective registration requires a float depth frame matching the IR model",
                  error);
    if (registered.data == nullptr || registered.format != Frame::Float ||
        registered.bytes_per_pixel != sizeof(float) || registered.width != target.width ||
        registered.height != target.height)
      return fail("registered output must be a float frame matching the target model", error);
    if (depth.data == registered.data)
      return fail("projective registration input and output buffers must not alias", error);

    const size_t output_count = static_cast<size_t>(target.width) * target.height;
    float* output = reinterpret_cast<float*>(registered.data);
    std::fill(output, output + output_count, 0.0f);
    // Per-thread tie-break scratch: reused across calls so a steady-state
    // consumer pays no allocation per frame, and concurrent applies on
    // different threads never share state. Retained capacity is bounded by
    // kRetainedScratchElements (covers the 1920x1080 Kinect color raster with
    // headroom, about 50 MB per thread); larger targets borrow function-local
    // vectors instead, so their scratch is released on every exit path rather
    // than pinned until thread exit.
    constexpr size_t kRetainedScratchElements = size_t(1) << 22;
    thread_local std::vector<float> retained_distance;
    thread_local std::vector<size_t> retained_source;
    std::vector<float> oversized_distance;
    std::vector<size_t> oversized_source;
    const bool retain = output_count <= kRetainedScratchElements;
    std::vector<float>& selected_distance = retain ? retained_distance : oversized_distance;
    std::vector<size_t>& selected_source = retain ? retained_source : oversized_source;
    try
    {
      selected_distance.assign(output_count, std::numeric_limits<float>::infinity());
      selected_source.assign(output_count, std::numeric_limits<size_t>::max());
    }
    catch (const std::exception& exception)
    {
      // Reported like every other failure here: this returns false with an
      // error rather than unwinding out of a bool-returning API.
      return fail(std::string("projective registration could not allocate tie-break scratch: ") +
                      exception.what(),
                  error);
    }

    const auto store = [&](int column, int row, float z_mm, float distance, size_t source_index)
    {
      if (column < 0 || row < 0 || column >= static_cast<int>(target.width) ||
          row >= static_cast<int>(target.height))
        return;
      const size_t output_index =
          static_cast<size_t>(row) * target.width + static_cast<size_t>(column);
      const float existing = output[output_index];
      const bool nearer = existing == 0.0f || z_mm < existing - 1e-4f;
      const bool equal_depth = existing != 0.0f && std::fabs(z_mm - existing) <= 1e-4f;
      const bool preferable = equal_depth && (distance < selected_distance[output_index] ||
                                              (distance == selected_distance[output_index] &&
                                               source_index < selected_source[output_index]));
      if (nearer || preferable)
      {
        output[output_index] = z_mm;
        selected_distance[output_index] = distance;
        selected_source[output_index] = source_index;
      }
    };

    const size_t source_count = static_cast<size_t>(source.width) * source.height;
    const float* source_depth = reinterpret_cast<const float*>(depth.data);
    for (size_t index = 0; index < source_count; ++index)
    {
      float depth_mm = source_depth[index];
      if (!validDepth(depth_mm))
        continue;
      if (options.apply_depth_correction)
        depth_mm = profile.depthCorrection().correct(depth_mm);
      if (!validDepth(depth_mm) || depth_mm < options.min_depth_mm ||
          depth_mm > options.max_depth_mm)
        continue;

      const double point[3] = {ray_x[index] * depth_mm, ray_y[index] * depth_mm, depth_mm};
      double projected[3];
      for (size_t row = 0; row < 3; ++row)
      {
        projected[row] =
            transform.rotation[row * 3] * point[0] + transform.rotation[row * 3 + 1] * point[1] +
            transform.rotation[row * 3 + 2] * point[2] + transform.translation_m[row] * 1000.0;
      }
      if (!std::isfinite(projected[2]) || projected[2] <= 0.0)
        continue;

      double x = projected[0] / projected[2];
      double y = projected[1] / projected[2];
      distort(target, x, y, x, y);
      const double pixel_x = target.fx * x + target.cx;
      const double pixel_y = target.fy * y + target.cy;
      if (!std::isfinite(pixel_x) || !std::isfinite(pixel_y))
        continue;
      const float z_mm = static_cast<float>(projected[2]);

      if (options.rasterization == RegistrationRasterization::Nearest)
      {
        const int column = static_cast<int>(std::floor(pixel_x + 0.5));
        const int row = static_cast<int>(std::floor(pixel_y + 0.5));
        const float dx = static_cast<float>(pixel_x - column);
        const float dy = static_cast<float>(pixel_y - row);
        store(column, row, z_mm, dx * dx + dy * dy, index);
      }
      else
      {
        const int x0 = static_cast<int>(std::floor(pixel_x));
        const int y0 = static_cast<int>(std::floor(pixel_y));
        for (int dy = 0; dy <= 1; ++dy)
        {
          for (int dx = 0; dx <= 1; ++dx)
          {
            const int column = x0 + dx;
            const int row = y0 + dy;
            const float delta_x = static_cast<float>(pixel_x - column);
            const float delta_y = static_cast<float>(pixel_y - row);
            store(column, row, z_mm, delta_x * delta_x + delta_y * delta_y, index);
          }
        }
      }
    }
    if (error != nullptr)
      error->clear();
    return true;
  }
};

std::unique_ptr<ProjectiveRegistration>
ProjectiveRegistration::create(const CalibrationProfile& profile,
                               const ProjectiveCameraModel& target,
                               const ProjectiveRegistrationOptions& options, std::string* error)
{
  auto impl = std::make_unique<ProjectiveRegistrationImpl>();
  impl->profile = profile;
  impl->target = target;
  impl->options = options;
  if (!impl->initialize(error))
    return nullptr;
  return std::unique_ptr<ProjectiveRegistration>(new ProjectiveRegistration(std::move(impl)));
}

ProjectiveRegistration::ProjectiveRegistration(std::unique_ptr<ProjectiveRegistrationImpl> impl)
    : impl_(std::move(impl))
{
}

ProjectiveRegistration::~ProjectiveRegistration() = default;

const ProjectiveCameraModel& ProjectiveRegistration::targetCamera() const
{
  return impl_->target;
}
const ProjectiveRegistrationOptions& ProjectiveRegistration::options() const
{
  return impl_->options;
}

bool ProjectiveRegistration::apply(const Frame& depth, Frame& registered_depth,
                                   std::string* error) const
{
  return impl_->apply(depth, registered_depth, error);
}

} // namespace libfreenect2
