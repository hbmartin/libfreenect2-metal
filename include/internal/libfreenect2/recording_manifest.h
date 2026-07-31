/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#ifndef LIBFREENECT2_RECORDING_MANIFEST_H_
#define LIBFREENECT2_RECORDING_MANIFEST_H_

#include <libfreenect2/libfreenect2.hpp>

#include <string>

namespace libfreenect2
{
namespace recording
{

struct ManifestV1
{
  ManifestV1();

  std::string serial;
  std::string firmware;
  std::string color_encoding;
  std::string depth_encoding;
  Freenect2Device::ColorCameraParams color;
  Freenect2Device::IrCameraParams ir;
  std::string p0_path;
  std::string device_clock;
  std::string arrival_clock;
};

bool serializeManifestV1(const ManifestV1& manifest, std::string& text, std::string* error = 0);
bool parseManifestV1(const std::string& text, ManifestV1& manifest, std::string* error = 0);

} // namespace recording
} // namespace libfreenect2

#endif // LIBFREENECT2_RECORDING_MANIFEST_H_
