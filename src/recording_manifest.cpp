/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#include <libfreenect2/recording_manifest.h>
#include <libfreenect2/recording_utils.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <stdexcept>

namespace libfreenect2
{
namespace recording
{
namespace
{

typedef nlohmann::json Json;

const char* const kDeviceClock = "kinect-v2-0.125ms-wrap32";
const char* const kArrivalClock = "monotonic-host-microseconds-relative-to-recording-start";

Json colorToJson(const Freenect2Device::ColorCameraParams& value)
{
  Json result;
#define LIBFREENECT2_COLOR_TO_JSON(field) result[#field] = value.field
  LIBFREENECT2_COLOR_TO_JSON(fx);
  LIBFREENECT2_COLOR_TO_JSON(fy);
  LIBFREENECT2_COLOR_TO_JSON(cx);
  LIBFREENECT2_COLOR_TO_JSON(cy);
  LIBFREENECT2_COLOR_TO_JSON(shift_d);
  LIBFREENECT2_COLOR_TO_JSON(shift_m);
  LIBFREENECT2_COLOR_TO_JSON(mx_x3y0);
  LIBFREENECT2_COLOR_TO_JSON(mx_x0y3);
  LIBFREENECT2_COLOR_TO_JSON(mx_x2y1);
  LIBFREENECT2_COLOR_TO_JSON(mx_x1y2);
  LIBFREENECT2_COLOR_TO_JSON(mx_x2y0);
  LIBFREENECT2_COLOR_TO_JSON(mx_x0y2);
  LIBFREENECT2_COLOR_TO_JSON(mx_x1y1);
  LIBFREENECT2_COLOR_TO_JSON(mx_x1y0);
  LIBFREENECT2_COLOR_TO_JSON(mx_x0y1);
  LIBFREENECT2_COLOR_TO_JSON(mx_x0y0);
  LIBFREENECT2_COLOR_TO_JSON(my_x3y0);
  LIBFREENECT2_COLOR_TO_JSON(my_x0y3);
  LIBFREENECT2_COLOR_TO_JSON(my_x2y1);
  LIBFREENECT2_COLOR_TO_JSON(my_x1y2);
  LIBFREENECT2_COLOR_TO_JSON(my_x2y0);
  LIBFREENECT2_COLOR_TO_JSON(my_x0y2);
  LIBFREENECT2_COLOR_TO_JSON(my_x1y1);
  LIBFREENECT2_COLOR_TO_JSON(my_x1y0);
  LIBFREENECT2_COLOR_TO_JSON(my_x0y1);
  LIBFREENECT2_COLOR_TO_JSON(my_x0y0);
#undef LIBFREENECT2_COLOR_TO_JSON
  return result;
}

Json irToJson(const Freenect2Device::IrCameraParams& value)
{
  Json result;
#define LIBFREENECT2_IR_TO_JSON(field) result[#field] = value.field
  LIBFREENECT2_IR_TO_JSON(fx);
  LIBFREENECT2_IR_TO_JSON(fy);
  LIBFREENECT2_IR_TO_JSON(cx);
  LIBFREENECT2_IR_TO_JSON(cy);
  LIBFREENECT2_IR_TO_JSON(k1);
  LIBFREENECT2_IR_TO_JSON(k2);
  LIBFREENECT2_IR_TO_JSON(k3);
  LIBFREENECT2_IR_TO_JSON(p1);
  LIBFREENECT2_IR_TO_JSON(p2);
#undef LIBFREENECT2_IR_TO_JSON
  return result;
}

float requireFiniteFloat(const Json& object, const char* name)
{
  // Convert through double with an explicit range check: nlohmann's get<float>()
  // is an unchecked static_cast, which is undefined behavior for out-of-range input.
  const Json& value = object.at(name);
  if (!value.is_number())
    throw std::domain_error(std::string("camera parameter is not a number: ") + name);
  const double parsed = value.get<double>();
  if (!std::isfinite(parsed) || parsed < -std::numeric_limits<float>::max() ||
      parsed > std::numeric_limits<float>::max())
    throw std::domain_error(std::string("camera parameter is not representable: ") + name);
  return static_cast<float>(parsed);
}

void colorFromJson(const Json& object, Freenect2Device::ColorCameraParams& value)
{
#define LIBFREENECT2_COLOR_FROM_JSON(field) value.field = requireFiniteFloat(object, #field)
  LIBFREENECT2_COLOR_FROM_JSON(fx);
  LIBFREENECT2_COLOR_FROM_JSON(fy);
  LIBFREENECT2_COLOR_FROM_JSON(cx);
  LIBFREENECT2_COLOR_FROM_JSON(cy);
  LIBFREENECT2_COLOR_FROM_JSON(shift_d);
  LIBFREENECT2_COLOR_FROM_JSON(shift_m);
  LIBFREENECT2_COLOR_FROM_JSON(mx_x3y0);
  LIBFREENECT2_COLOR_FROM_JSON(mx_x0y3);
  LIBFREENECT2_COLOR_FROM_JSON(mx_x2y1);
  LIBFREENECT2_COLOR_FROM_JSON(mx_x1y2);
  LIBFREENECT2_COLOR_FROM_JSON(mx_x2y0);
  LIBFREENECT2_COLOR_FROM_JSON(mx_x0y2);
  LIBFREENECT2_COLOR_FROM_JSON(mx_x1y1);
  LIBFREENECT2_COLOR_FROM_JSON(mx_x1y0);
  LIBFREENECT2_COLOR_FROM_JSON(mx_x0y1);
  LIBFREENECT2_COLOR_FROM_JSON(mx_x0y0);
  LIBFREENECT2_COLOR_FROM_JSON(my_x3y0);
  LIBFREENECT2_COLOR_FROM_JSON(my_x0y3);
  LIBFREENECT2_COLOR_FROM_JSON(my_x2y1);
  LIBFREENECT2_COLOR_FROM_JSON(my_x1y2);
  LIBFREENECT2_COLOR_FROM_JSON(my_x2y0);
  LIBFREENECT2_COLOR_FROM_JSON(my_x0y2);
  LIBFREENECT2_COLOR_FROM_JSON(my_x1y1);
  LIBFREENECT2_COLOR_FROM_JSON(my_x1y0);
  LIBFREENECT2_COLOR_FROM_JSON(my_x0y1);
  LIBFREENECT2_COLOR_FROM_JSON(my_x0y0);
#undef LIBFREENECT2_COLOR_FROM_JSON
}

void irFromJson(const Json& object, Freenect2Device::IrCameraParams& value)
{
#define LIBFREENECT2_IR_FROM_JSON(field) value.field = requireFiniteFloat(object, #field)
  LIBFREENECT2_IR_FROM_JSON(fx);
  LIBFREENECT2_IR_FROM_JSON(fy);
  LIBFREENECT2_IR_FROM_JSON(cx);
  LIBFREENECT2_IR_FROM_JSON(cy);
  LIBFREENECT2_IR_FROM_JSON(k1);
  LIBFREENECT2_IR_FROM_JSON(k2);
  LIBFREENECT2_IR_FROM_JSON(k3);
  LIBFREENECT2_IR_FROM_JSON(p1);
  LIBFREENECT2_IR_FROM_JSON(p2);
#undef LIBFREENECT2_IR_FROM_JSON
}

bool allFinite(const Json& object, std::string* error)
{
  for (Json::const_iterator entry = object.begin(); entry != object.end(); ++entry)
  {
    if (!entry->is_number() || !std::isfinite(entry->get<double>()))
    {
      if (error != 0)
        *error = "recording manifest contains a non-finite camera parameter: " + entry.key();
      return false;
    }
  }
  return true;
}

bool validateManifest(const ManifestV1& manifest, uint32_t version, std::string* error)
{
  if (manifest.serial.empty() || manifest.firmware.empty())
  {
    if (error != 0)
      *error = "recording manifest requires device serial and firmware";
    return false;
  }
  if (!allFinite(colorToJson(manifest.color), error) || !allFinite(irToJson(manifest.ir), error))
    return false;
  if (manifest.color_encoding != "jpeg" || manifest.depth_encoding != "kinect-v2-raw")
  {
    if (error != 0)
      *error = "recording manifest contains an unsupported stream encoding";
    return false;
  }
  if (!isSafeRelativePath(manifest.p0_path))
  {
    if (error != 0)
      *error = "recording manifest contains an unsafe P0 path";
    return false;
  }
  if (version == 2 && !manifest.profile_path.empty() &&
      !isSafeRelativePath(manifest.profile_path))
  {
    if (error != 0)
      *error = "recording manifest contains an unsafe calibration profile path";
    return false;
  }
  if (manifest.device_clock != kDeviceClock || manifest.arrival_clock != kArrivalClock)
  {
    if (error != 0)
      *error = "recording manifest declares unsupported clock semantics";
    return false;
  }
  return true;
}

} // namespace

ManifestV1::ManifestV1()
    : version(1), color_encoding("jpeg"), depth_encoding("kinect-v2-raw"),
      p0_path("calibration/p0.bin"),
      device_clock(kDeviceClock), arrival_clock(kArrivalClock)
{
  std::memset(&color, 0, sizeof(color));
  std::memset(&ir, 0, sizeof(ir));
}

bool serializeManifestV1(const ManifestV1& manifest, std::string& text, std::string* error)
{
  if (!validateManifest(manifest, 1, error))
    return false;

  try
  {
    Json root;
    root["version"] = 1;
    root["device"] = {{"serial", manifest.serial}, {"firmware", manifest.firmware}};
    root["streams"] = {{"color", {{"encoding", manifest.color_encoding}}},
                       {"depth", {{"encoding", manifest.depth_encoding}}}};
    root["calibration"] = {{"color", colorToJson(manifest.color)},
                           {"ir", irToJson(manifest.ir)},
                           {"p0", manifest.p0_path}};
    root["clocks"] = {{"device", manifest.device_clock}, {"arrival", manifest.arrival_clock}};
    text = root.dump(2) + "\n";
    return true;
  }
  catch (const std::exception& exception)
  {
    if (error != 0)
      *error = std::string("failed to serialize recording manifest: ") + exception.what();
    return false;
  }
}

bool serializeManifestV2(const ManifestV1& manifest, std::string& text, std::string* error)
{
  if (!validateManifest(manifest, 2, error))
    return false;

  try
  {
    Json root;
    root["version"] = 2;
    root["device"] = {{"serial", manifest.serial}, {"firmware", manifest.firmware}};
    root["streams"] = {{"color", {{"encoding", manifest.color_encoding}}},
                       {"depth", {{"encoding", manifest.depth_encoding}}}};
    root["calibration"] = {{"color", colorToJson(manifest.color)},
                           {"ir", irToJson(manifest.ir)},
                           {"p0", manifest.p0_path}};
    if (!manifest.profile_path.empty())
      root["calibration"]["profile"] = manifest.profile_path;
    root["clocks"] = {{"device", manifest.device_clock}, {"arrival", manifest.arrival_clock}};
    text = root.dump(2) + "\n";
    return true;
  }
  catch (const std::exception& exception)
  {
    if (error != 0)
      *error = std::string("failed to serialize recording manifest: ") + exception.what();
    return false;
  }
}

bool parseManifest(const std::string& text, ManifestV1& manifest, std::string* error)
{
  try
  {
    const Json root = Json::parse(text);
    const Json& version_value = root.at("version");
    if (!version_value.is_number_integer())
    {
      if (error != 0)
        *error = "unsupported recording manifest version: version must be an integer";
      return false;
    }
    const int64_t version = version_value.get<int64_t>();
    if (version != 1 && version != 2)
    {
      if (error != 0)
        *error = "unsupported recording manifest version";
      return false;
    }

    ManifestV1 parsed;
    parsed.version = static_cast<uint32_t>(version);
    parsed.serial = root.at("device").at("serial").get<std::string>();
    parsed.firmware = root.at("device").at("firmware").get<std::string>();
    parsed.color_encoding = root.at("streams").at("color").at("encoding").get<std::string>();
    parsed.depth_encoding = root.at("streams").at("depth").at("encoding").get<std::string>();
    colorFromJson(root.at("calibration").at("color"), parsed.color);
    irFromJson(root.at("calibration").at("ir"), parsed.ir);
    parsed.p0_path = root.at("calibration").at("p0").get<std::string>();
    if (version == 2 && root.at("calibration").contains("profile"))
      parsed.profile_path = root.at("calibration").at("profile").get<std::string>();
    parsed.device_clock = root.at("clocks").at("device").get<std::string>();
    parsed.arrival_clock = root.at("clocks").at("arrival").get<std::string>();
    if (!validateManifest(parsed, parsed.version, error))
      return false;
    manifest = parsed;
    return true;
  }
  catch (const std::exception& exception)
  {
    if (error != 0)
      *error = std::string("invalid recording manifest: ") + exception.what();
    return false;
  }
}

bool parseManifestV1(const std::string& text, ManifestV1& manifest, std::string* error)
{
  ManifestV1 parsed;
  if (!parseManifest(text, parsed, error))
    return false;
  if (parsed.version != 1)
  {
    if (error != 0)
      *error = "unsupported recording manifest version";
    return false;
  }
  manifest = parsed;
  return true;
}

} // namespace recording
} // namespace libfreenect2
