/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#ifndef LIBFREENECT2_RECORDING_LOADER_H_
#define LIBFREENECT2_RECORDING_LOADER_H_

#include <libfreenect2/libfreenect2.hpp>
#include <libfreenect2/calibration_profile.h>
#include <libfreenect2/recording_journal.h>
#include <libfreenect2/recording_manifest.h>

#include <string>
#include <vector>

namespace libfreenect2
{
namespace recording
{

struct RecordingMetadata
{
  std::string directory;
  ManifestV1 manifest;
  CalibrationData calibration;
  bool has_profile;
  CalibrationProfile profile;

  RecordingMetadata() : has_profile(false) {}
};

struct RecordingData : RecordingMetadata
{
  std::vector<JournalEntry> entries;
};

bool loadRecordingMetadata(const std::string& directory, bool salvage_incomplete,
                           RecordingMetadata& metadata, std::string* error = 0);
bool loadRecordingData(const std::string& directory, bool salvage_incomplete,
                       RecordingData& recording, std::string* error = 0);

} // namespace recording
} // namespace libfreenect2

#endif // LIBFREENECT2_RECORDING_LOADER_H_
