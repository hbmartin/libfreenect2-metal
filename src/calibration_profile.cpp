/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#include <libfreenect2/calibration_profile.h>
#include <libfreenect2/recording_utils.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <utility>
#include <vector>

namespace libfreenect2
{
namespace
{

using Json = nlohmann::json;

bool fail(const std::string& message, std::string* error)
{
  if (error != nullptr)
    *error = message;
  return false;
}

bool finite(double value)
{
  return std::isfinite(value);
}

const char* distortionName(DistortionModel model)
{
  switch (model)
  {
  case DistortionModel::None:
    return "none";
  case DistortionModel::BrownConrady5:
    return "brown_conrady_5";
  case DistortionModel::Rational8:
    return "rational_8";
  }
  return "invalid";
}

bool parseDistortionName(const std::string& name, DistortionModel& model)
{
  if (name == "none")
    model = DistortionModel::None;
  else if (name == "brown_conrady_5")
    model = DistortionModel::BrownConrady5;
  else if (name == "rational_8")
    model = DistortionModel::Rational8;
  else
    return false;
  return true;
}

Json cameraToJson(const ProjectiveCameraModel& camera)
{
  const size_t count = camera.distortion_model == DistortionModel::None
                           ? 0
                           : (camera.distortion_model == DistortionModel::BrownConrady5 ? 5 : 8);
  Json coefficients = Json::array();
  for (size_t index = 0; index < count; ++index)
    coefficients.push_back(camera.distortion[index]);
  return {{"width", camera.width},
          {"height", camera.height},
          {"fx", camera.fx},
          {"fy", camera.fy},
          {"cx", camera.cx},
          {"cy", camera.cy},
          {"distortion_model", distortionName(camera.distortion_model)},
          {"distortion", coefficients}};
}

uint32_t requireUint32(const Json& value, const char* field)
{
  if (!value.is_number_unsigned() && !value.is_number_integer())
    throw std::domain_error(std::string(field) + " must be an unsigned integer");
  if (value.is_number_unsigned())
  {
    const uint64_t parsed = value.get<uint64_t>();
    if (parsed > std::numeric_limits<uint32_t>::max())
      throw std::domain_error(std::string(field) + " is outside the uint32 range");
    return static_cast<uint32_t>(parsed);
  }
  const int64_t parsed = value.get<int64_t>();
  if (parsed < 0 || static_cast<uint64_t>(parsed) > std::numeric_limits<uint32_t>::max())
    throw std::domain_error(std::string(field) + " is outside the uint32 range");
  return static_cast<uint32_t>(parsed);
}

double requireFinite(const Json& value, const char* field)
{
  if (!value.is_number())
    throw std::domain_error(std::string(field) + " must be numeric");
  const double parsed = value.get<double>();
  if (!finite(parsed))
    throw std::domain_error(std::string(field) + " must be finite");
  return parsed;
}

ProjectiveCameraModel cameraFromJson(const Json& value)
{
  ProjectiveCameraModel camera;
  camera.width = requireUint32(value.at("width"), "camera width");
  camera.height = requireUint32(value.at("height"), "camera height");
  camera.fx = requireFinite(value.at("fx"), "camera fx");
  camera.fy = requireFinite(value.at("fy"), "camera fy");
  camera.cx = requireFinite(value.at("cx"), "camera cx");
  camera.cy = requireFinite(value.at("cy"), "camera cy");
  if (!parseDistortionName(value.at("distortion_model").get<std::string>(),
                           camera.distortion_model))
    throw std::domain_error("unsupported camera distortion model");
  const Json& coefficients = value.at("distortion");
  const size_t expected = camera.distortion_model == DistortionModel::None
                              ? 0
                              : (camera.distortion_model == DistortionModel::BrownConrady5 ? 5 : 8);
  if (!coefficients.is_array() || coefficients.size() != expected)
    throw std::domain_error("camera distortion coefficient count does not match its model");
  for (size_t index = 0; index < expected; ++index)
    camera.distortion[index] = requireFinite(coefficients[index], "camera distortion");
  return camera;
}

Json transformToJson(const RigidTransform& transform)
{
  return {{"rotation_row_major", transform.rotation},
          {"translation_m", transform.translation_m}};
}

RigidTransform transformFromJson(const Json& value)
{
  RigidTransform transform;
  const Json& rotation = value.at("rotation_row_major");
  const Json& translation = value.at("translation_m");
  if (!rotation.is_array() || rotation.size() != transform.rotation.size() ||
      !translation.is_array() || translation.size() != transform.translation_m.size())
    throw std::domain_error("depth-to-color transform has invalid dimensions");
  for (size_t index = 0; index < transform.rotation.size(); ++index)
    transform.rotation[index] = requireFinite(rotation[index], "rotation");
  for (size_t index = 0; index < transform.translation_m.size(); ++index)
    transform.translation_m[index] = requireFinite(translation[index], "translation");
  return transform;
}

Json correctionToJson(const DepthCorrectionProfile& correction)
{
  return {{"model", correction.model == DepthCorrectionProfile::OffsetOnly ? "offset_only"
                                                                           : "linear"},
          {"scale", correction.scale},
          {"offset_mm", correction.offset_mm},
          {"rmse_mm", correction.rmse_mm}};
}

DepthCorrectionProfile correctionFromJson(const Json& value, const std::string& serial,
                                          const std::string& firmware)
{
  DepthCorrectionProfile correction;
  const std::string model = value.at("model").get<std::string>();
  if (model == "offset_only")
    correction.model = DepthCorrectionProfile::OffsetOnly;
  else if (model == "linear")
    correction.model = DepthCorrectionProfile::Linear;
  else
    throw std::domain_error("unsupported depth correction model");
  correction.serial = serial;
  correction.firmware = firmware;
  correction.scale = requireFinite(value.at("scale"), "depth correction scale");
  correction.offset_mm = requireFinite(value.at("offset_mm"), "depth correction offset");
  correction.rmse_mm = requireFinite(value.at("rmse_mm"), "depth correction RMSE");
  return correction;
}

Json qualityToJson(const CalibrationQualityMetrics& quality)
{
  return {{"color_views", quality.color_views},
          {"ir_views", quality.ir_views},
          {"stereo_views", quality.stereo_views},
          {"depth_views", quality.depth_views},
          {"color_rms_px", quality.color_rms_px},
          {"ir_rms_px", quality.ir_rms_px},
          {"held_out_stereo_rms_px", quality.held_out_stereo_rms_px},
          {"depth_rmse_mm", quality.depth_rmse_mm}};
}

CalibrationQualityMetrics qualityFromJson(const Json& value)
{
  CalibrationQualityMetrics quality;
  quality.color_views = requireUint32(value.at("color_views"), "color view count");
  quality.ir_views = requireUint32(value.at("ir_views"), "IR view count");
  quality.stereo_views = requireUint32(value.at("stereo_views"), "stereo view count");
  quality.depth_views = requireUint32(value.at("depth_views"), "depth view count");
  quality.color_rms_px = requireFinite(value.at("color_rms_px"), "color RMS");
  quality.ir_rms_px = requireFinite(value.at("ir_rms_px"), "IR RMS");
  quality.held_out_stereo_rms_px =
      requireFinite(value.at("held_out_stereo_rms_px"), "held-out stereo RMS");
  quality.depth_rmse_mm = requireFinite(value.at("depth_rmse_mm"), "depth RMSE");
  return quality;
}

} // namespace

ProjectiveCameraModel::ProjectiveCameraModel()
    : width(0), height(0), fx(0.0), fy(0.0), cx(0.0), cy(0.0),
      distortion_model(DistortionModel::None), distortion{}
{
}

bool ProjectiveCameraModel::isValid(std::string* error) const
{
  if (width == 0 || height == 0 || width > 16384 || height > 16384)
    return fail("camera resolution is invalid", error);
  if (!finite(fx) || !finite(fy) || fx <= 0.0 || fy <= 0.0 || !finite(cx) || !finite(cy))
    return fail("camera intrinsics must be finite with positive focal lengths", error);
  if (distortion_model != DistortionModel::None &&
      distortion_model != DistortionModel::BrownConrady5 &&
      distortion_model != DistortionModel::Rational8)
    return fail("camera distortion model is invalid", error);
  const size_t count = distortion_model == DistortionModel::None
                           ? 0
                           : (distortion_model == DistortionModel::BrownConrady5 ? 5 : 8);
  for (size_t index = 0; index < count; ++index)
  {
    if (!finite(distortion[index]))
      return fail("camera distortion coefficients must be finite", error);
  }
  if (error != nullptr)
    error->clear();
  return true;
}

ProjectiveCameraModel ProjectiveCameraModel::scaledTo(uint32_t scaled_width,
                                                       uint32_t scaled_height) const
{
  ProjectiveCameraModel scaled = *this;
  if (width == 0 || height == 0)
  {
    scaled.width = scaled_width;
    scaled.height = scaled_height;
    return scaled;
  }
  const double scale_x = static_cast<double>(scaled_width) / width;
  const double scale_y = static_cast<double>(scaled_height) / height;
  scaled.width = scaled_width;
  scaled.height = scaled_height;
  scaled.fx *= scale_x;
  scaled.fy *= scale_y;
  scaled.cx = (scaled.cx + 0.5) * scale_x - 0.5;
  scaled.cy = (scaled.cy + 0.5) * scale_y - 0.5;
  return scaled;
}

ProjectiveCameraModel ProjectiveCameraModel::rectified() const
{
  ProjectiveCameraModel result = *this;
  result.distortion_model = DistortionModel::None;
  result.distortion.fill(0.0);
  return result;
}

RigidTransform::RigidTransform() : rotation{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0},
                                   translation_m{0.0, 0.0, 0.0}
{
}

bool RigidTransform::isValid(std::string* error) const
{
  for (double value : rotation)
  {
    if (!finite(value))
      return fail("rotation contains a non-finite value", error);
  }
  for (double value : translation_m)
  {
    if (!finite(value))
      return fail("translation contains a non-finite value", error);
  }

  for (size_t row = 0; row < 3; ++row)
  {
    for (size_t column = 0; column < 3; ++column)
    {
      double dot = 0.0;
      for (size_t index = 0; index < 3; ++index)
        dot += rotation[index * 3 + row] * rotation[index * 3 + column];
      const double expected = row == column ? 1.0 : 0.0;
      if (std::fabs(dot - expected) > 1e-4)
        return fail("rotation is not orthonormal", error);
    }
  }
  const double determinant =
      rotation[0] * (rotation[4] * rotation[8] - rotation[5] * rotation[7]) -
      rotation[1] * (rotation[3] * rotation[8] - rotation[5] * rotation[6]) +
      rotation[2] * (rotation[3] * rotation[7] - rotation[4] * rotation[6]);
  if (std::fabs(determinant - 1.0) > 1e-4)
    return fail("rotation determinant is not one", error);
  if (error != nullptr)
    error->clear();
  return true;
}

CalibrationQualityMetrics::CalibrationQualityMetrics()
    : color_views(0), ir_views(0), stereo_views(0), depth_views(0), color_rms_px(0.0),
      ir_rms_px(0.0), held_out_stereo_rms_px(0.0), depth_rmse_mm(0.0)
{
}

bool CalibrationQualityMetrics::isValid(std::string* error) const
{
  if (!finite(color_rms_px) || color_rms_px < 0.0 || !finite(ir_rms_px) || ir_rms_px < 0.0 ||
      !finite(held_out_stereo_rms_px) || held_out_stereo_rms_px < 0.0 ||
      !finite(depth_rmse_mm) || depth_rmse_mm < 0.0)
    return fail("calibration quality metrics must be finite and non-negative", error);
  if (error != nullptr)
    error->clear();
  return true;
}

class CalibrationProfileImpl
{
public:
  uint32_t schema_version = 1;
  std::string serial;
  std::string firmware;
  ProjectiveCameraModel color;
  ProjectiveCameraModel ir;
  RigidTransform depth_to_color;
  bool has_depth_correction = false;
  DepthCorrectionProfile depth_correction;
  bool has_quality = false;
  CalibrationQualityMetrics quality;
  std::string created_utc;
  std::string tool_version;
  std::string job_sha256;
};

CalibrationProfile::CalibrationProfile() : impl_(std::make_unique<CalibrationProfileImpl>()) {}
CalibrationProfile::CalibrationProfile(const CalibrationProfile& other)
    : impl_(std::make_unique<CalibrationProfileImpl>(*other.impl_))
{
}
CalibrationProfile::CalibrationProfile(CalibrationProfile&& other) noexcept = default;
CalibrationProfile& CalibrationProfile::operator=(const CalibrationProfile& other)
{
  if (this != &other)
  {
    if (impl_)
      *impl_ = *other.impl_;
    else
      impl_ = std::make_unique<CalibrationProfileImpl>(*other.impl_);
  }
  return *this;
}
CalibrationProfile& CalibrationProfile::operator=(CalibrationProfile&& other) noexcept = default;
CalibrationProfile::~CalibrationProfile() = default;

uint32_t CalibrationProfile::schemaVersion() const { return impl_->schema_version; }
const std::string& CalibrationProfile::serial() const { return impl_->serial; }
const std::string& CalibrationProfile::firmware() const { return impl_->firmware; }
void CalibrationProfile::setDeviceIdentity(const std::string& serial, const std::string& firmware)
{
  impl_->serial = serial;
  impl_->firmware = firmware;
}
const ProjectiveCameraModel& CalibrationProfile::colorCamera() const { return impl_->color; }
const ProjectiveCameraModel& CalibrationProfile::irCamera() const { return impl_->ir; }
const RigidTransform& CalibrationProfile::depthToColor() const { return impl_->depth_to_color; }
void CalibrationProfile::setColorCamera(const ProjectiveCameraModel& camera)
{
  impl_->color = camera;
}
void CalibrationProfile::setIrCamera(const ProjectiveCameraModel& camera) { impl_->ir = camera; }
void CalibrationProfile::setDepthToColor(const RigidTransform& transform)
{
  impl_->depth_to_color = transform;
}
bool CalibrationProfile::hasDepthCorrection() const { return impl_->has_depth_correction; }
const DepthCorrectionProfile& CalibrationProfile::depthCorrection() const
{
  return impl_->depth_correction;
}
void CalibrationProfile::setDepthCorrection(const DepthCorrectionProfile& correction)
{
  impl_->depth_correction = correction;
  impl_->has_depth_correction = true;
}
void CalibrationProfile::clearDepthCorrection() { impl_->has_depth_correction = false; }
bool CalibrationProfile::hasQualityMetrics() const { return impl_->has_quality; }
const CalibrationQualityMetrics& CalibrationProfile::qualityMetrics() const
{
  return impl_->quality;
}
void CalibrationProfile::setQualityMetrics(const CalibrationQualityMetrics& quality)
{
  impl_->quality = quality;
  impl_->has_quality = true;
}
void CalibrationProfile::clearQualityMetrics() { impl_->has_quality = false; }
const std::string& CalibrationProfile::createdUtc() const { return impl_->created_utc; }
const std::string& CalibrationProfile::toolVersion() const { return impl_->tool_version; }
const std::string& CalibrationProfile::jobSha256() const { return impl_->job_sha256; }
void CalibrationProfile::setProvenance(const std::string& created_utc,
                                       const std::string& tool_version,
                                       const std::string& job_sha256)
{
  impl_->created_utc = created_utc;
  impl_->tool_version = tool_version;
  impl_->job_sha256 = job_sha256;
}

bool CalibrationProfile::isValid(std::string* error) const
{
  if (impl_->schema_version != 1)
    return fail("unsupported calibration profile version", error);
  if (impl_->serial.empty())
    return fail("calibration profile requires a device serial", error);
  if (!impl_->color.isValid(error) || !impl_->ir.isValid(error) ||
      !impl_->depth_to_color.isValid(error))
    return false;
  if (impl_->has_depth_correction)
  {
    if (!impl_->depth_correction.isValid())
      return fail("calibration profile contains an invalid depth correction", error);
    if (!impl_->depth_correction.serial.empty() &&
        impl_->depth_correction.serial != impl_->serial)
      return fail("depth correction serial does not match the calibration profile", error);
  }
  if (impl_->has_quality && !impl_->quality.isValid(error))
    return false;
  if (error != nullptr)
    error->clear();
  return true;
}

bool CalibrationProfile::matchesDevice(const std::string& serial, const std::string& firmware,
                                       bool allow_serial_mismatch, std::string* warning,
                                       std::string* error) const
{
  if (!isValid(error))
    return false;
  if (warning != nullptr)
    warning->clear();
  if (serial != impl_->serial)
  {
    if (!allow_serial_mismatch)
      return fail("calibration profile serial does not match the device", error);
    if (warning != nullptr)
      *warning = "calibration profile serial mismatch was explicitly allowed";
  }
  if (!firmware.empty() && !impl_->firmware.empty() && firmware != impl_->firmware)
  {
    if (warning != nullptr)
    {
      if (!warning->empty())
        *warning += "; ";
      *warning += "calibration profile firmware differs from the device";
    }
  }
  if (error != nullptr)
    error->clear();
  return true;
}

bool CalibrationProfile::save(const std::string& path, std::string* error) const
{
  if (path.empty())
    return fail("calibration profile path is empty", error);
  if (!isValid(error))
    return false;
  try
  {
    Json root;
    root["schema"] = "libfreenect2.calibration-profile";
    root["version"] = impl_->schema_version;
    root["device"] = {{"serial", impl_->serial}, {"firmware", impl_->firmware}};
    root["cameras"] = {{"color", cameraToJson(impl_->color)}, {"ir", cameraToJson(impl_->ir)}};
    root["depth_to_color"] = transformToJson(impl_->depth_to_color);
    if (impl_->has_depth_correction)
      root["depth_correction"] = correctionToJson(impl_->depth_correction);
    if (impl_->has_quality)
      root["quality"] = qualityToJson(impl_->quality);
    root["provenance"] = {{"created_utc", impl_->created_utc},
                           {"tool_version", impl_->tool_version},
                           {"job_sha256", impl_->job_sha256}};
    return recording::writeFileAtomically(path, root.dump(2) + "\n", error);
  }
  catch (const std::exception& exception)
  {
    return fail(std::string("failed to serialize calibration profile: ") + exception.what(), error);
  }
}

bool CalibrationProfile::load(const std::string& path, CalibrationProfile& profile,
                              std::string* error)
{
  std::vector<unsigned char> bytes;
  if (!recording::readFile(path, bytes, error))
    return false;
  try
  {
    const Json root = Json::parse(bytes.begin(), bytes.end());
    if (root.at("schema").get<std::string>() != "libfreenect2.calibration-profile" ||
        requireUint32(root.at("version"), "profile version") != 1)
      return fail("unsupported calibration profile version", error);

    CalibrationProfile loaded;
    loaded.impl_->serial = root.at("device").at("serial").get<std::string>();
    loaded.impl_->firmware = root.at("device").at("firmware").get<std::string>();
    loaded.impl_->color = cameraFromJson(root.at("cameras").at("color"));
    loaded.impl_->ir = cameraFromJson(root.at("cameras").at("ir"));
    loaded.impl_->depth_to_color = transformFromJson(root.at("depth_to_color"));
    if (root.contains("depth_correction") && !root.at("depth_correction").is_null())
    {
      loaded.impl_->depth_correction = correctionFromJson(
          root.at("depth_correction"), loaded.impl_->serial, loaded.impl_->firmware);
      loaded.impl_->has_depth_correction = true;
    }
    if (root.contains("quality") && !root.at("quality").is_null())
    {
      loaded.impl_->quality = qualityFromJson(root.at("quality"));
      loaded.impl_->has_quality = true;
    }
    if (root.contains("provenance"))
    {
      const Json& provenance = root.at("provenance");
      loaded.impl_->created_utc = provenance.value("created_utc", std::string());
      loaded.impl_->tool_version = provenance.value("tool_version", std::string());
      loaded.impl_->job_sha256 = provenance.value("job_sha256", std::string());
    }
    if (!loaded.isValid(error))
      return false;
    profile = std::move(loaded);
    if (error != nullptr)
      error->clear();
    return true;
  }
  catch (const std::exception& exception)
  {
    return fail(std::string("invalid calibration profile: ") + exception.what(), error);
  }
}

} // namespace libfreenect2
