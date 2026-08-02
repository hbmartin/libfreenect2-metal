/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#ifndef LIBFREENECT2_PROJECTIVE_REGISTRATION_H_
#define LIBFREENECT2_PROJECTIVE_REGISTRATION_H_

#include <libfreenect2/calibration_profile.h>
#include <libfreenect2/frame_listener.hpp>

#include <memory>
#include <string>

namespace libfreenect2
{

enum class RegistrationRasterization : uint32_t
{
  Nearest = 0,
  FourNeighborSplat = 1
};

struct LIBFREENECT2_API ProjectiveRegistrationOptions
{
  ProjectiveRegistrationOptions();

  RegistrationRasterization rasterization;
  float min_depth_mm;
  float max_depth_mm;
  bool apply_depth_correction;

  bool isValid(std::string* error = nullptr) const;
};

class ProjectiveRegistrationImpl;

/** Conventional extrinsic/projective depth registration.
 *
 * Unlike Registration, this class consumes ordinary camera matrices and a
 * rigid depth-to-color transform. The factory-calibrated polynomial mapping
 * remains available through Registration.
 */
class LIBFREENECT2_API ProjectiveRegistration
{
public:
  static std::unique_ptr<ProjectiveRegistration>
  create(const CalibrationProfile& profile, const ProjectiveCameraModel& target,
         const ProjectiveRegistrationOptions& options = ProjectiveRegistrationOptions(),
         std::string* error = nullptr);

  ~ProjectiveRegistration();

  const ProjectiveCameraModel& targetCamera() const;
  const ProjectiveRegistrationOptions& options() const;

  /** Register decoded float depth into a preallocated float target frame.
   *
   * Output pixels contain target-camera Z in millimeters. Missing pixels are
   * zero. Calls are thread-safe when the caller supplies distinct outputs.
   */
  bool apply(const Frame& depth, Frame& registered_depth, std::string* error = nullptr) const;

private:
  explicit ProjectiveRegistration(std::unique_ptr<ProjectiveRegistrationImpl> impl);
  std::unique_ptr<ProjectiveRegistrationImpl> impl_;

  ProjectiveRegistration(const ProjectiveRegistration&) = delete;
  ProjectiveRegistration& operator=(const ProjectiveRegistration&) = delete;
};

} // namespace libfreenect2

#endif // LIBFREENECT2_PROJECTIVE_REGISTRATION_H_
