/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#ifndef LIBFREENECT2_CALIBRATION_PROFILE_H_
#define LIBFREENECT2_CALIBRATION_PROFILE_H_

#include <libfreenect2/config.h>
#include <libfreenect2/depth_calibration.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace libfreenect2
{

/** Distortion conventions supported by conventional projective calibration. */
enum class DistortionModel : uint32_t
{
  None = 0,
  BrownConrady5 = 1,
  Rational8 = 2
};

/** Resolution, pinhole intrinsics, and distortion for one camera. */
struct LIBFREENECT2_API ProjectiveCameraModel
{
  ProjectiveCameraModel();

  uint32_t width;
  uint32_t height;
  double fx;
  double fy;
  double cx;
  double cy;
  DistortionModel distortion_model;
  std::array<double, 8> distortion;

  bool isValid(std::string* error = nullptr) const;

  /** Scale the raster and intrinsics while preserving pixel-center geometry. */
  ProjectiveCameraModel scaledTo(uint32_t scaled_width, uint32_t scaled_height) const;

  /** Return a model with the same pinhole geometry and no output distortion. */
  ProjectiveCameraModel rectified() const;
};

/** Rigid transform with row-major rotation and translation in meters. */
struct LIBFREENECT2_API RigidTransform
{
  RigidTransform();

  std::array<double, 9> rotation;
  std::array<double, 3> translation_m;

  bool isValid(std::string* error = nullptr) const;
};

/** Aggregate validation measurements retained in a canonical profile. */
struct LIBFREENECT2_API CalibrationQualityMetrics
{
  CalibrationQualityMetrics();

  uint32_t color_views;
  uint32_t ir_views;
  uint32_t stereo_views;
  uint32_t depth_views;
  double color_rms_px;
  double ir_rms_px;
  double held_out_stereo_rms_px;
  double depth_rmse_mm;

  bool isValid(std::string* error = nullptr) const;
};

class CalibrationProfileImpl;

/** Versioned user calibration for conventional depth-to-color geometry. */
class LIBFREENECT2_API CalibrationProfile
{
public:
  CalibrationProfile();
  CalibrationProfile(const CalibrationProfile& other);
  CalibrationProfile(CalibrationProfile&& other) noexcept;
  CalibrationProfile& operator=(const CalibrationProfile& other);
  CalibrationProfile& operator=(CalibrationProfile&& other) noexcept;
  ~CalibrationProfile();

  uint32_t schemaVersion() const;

  const std::string& serial() const;
  const std::string& firmware() const;
  void setDeviceIdentity(const std::string& serial, const std::string& firmware);

  const ProjectiveCameraModel& colorCamera() const;
  const ProjectiveCameraModel& irCamera() const;
  const RigidTransform& depthToColor() const;
  void setColorCamera(const ProjectiveCameraModel& camera);
  void setIrCamera(const ProjectiveCameraModel& camera);
  void setDepthToColor(const RigidTransform& transform);

  bool hasDepthCorrection() const;
  const DepthCorrectionProfile& depthCorrection() const;
  void setDepthCorrection(const DepthCorrectionProfile& correction);
  void clearDepthCorrection();

  bool hasQualityMetrics() const;
  const CalibrationQualityMetrics& qualityMetrics() const;
  void setQualityMetrics(const CalibrationQualityMetrics& quality);
  void clearQualityMetrics();

  const std::string& createdUtc() const;
  const std::string& toolVersion() const;
  const std::string& jobSha256() const;
  void setProvenance(const std::string& created_utc, const std::string& tool_version,
                     const std::string& job_sha256);

  bool isValid(std::string* error = nullptr) const;

  /** Check binding identity. Firmware mismatches are returned as warnings. */
  bool matchesDevice(const std::string& serial, const std::string& firmware,
                     bool allow_serial_mismatch, std::string* warning = nullptr,
                     std::string* error = nullptr) const;

  bool save(const std::string& path, std::string* error = nullptr) const;
  static bool load(const std::string& path, CalibrationProfile& profile,
                   std::string* error = nullptr);

private:
  std::unique_ptr<CalibrationProfileImpl> impl_;
};

} // namespace libfreenect2

#endif // LIBFREENECT2_CALIBRATION_PROFILE_H_
