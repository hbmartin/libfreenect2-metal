/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#ifndef LIBFREENECT2_RECORDING_LOADER_H_
#define LIBFREENECT2_RECORDING_LOADER_H_

#include <libfreenect2/libfreenect2.hpp>
#include <libfreenect2/recording_manifest.h>

#include <string>

namespace libfreenect2
{
namespace recording
{

struct RecordingMetadata
{
  std::string directory;
  ManifestV1 manifest;
  CalibrationData calibration;
};

bool loadRecordingMetadata(const std::string& directory, bool salvage_incomplete,
                           RecordingMetadata& metadata, std::string* error = 0);

} // namespace recording
} // namespace libfreenect2

#endif // LIBFREENECT2_RECORDING_LOADER_H_
