/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#include <libfreenect2/recording_loader.h>

#include <libfreenect2/protocol/response.h>
#include <libfreenect2/recording_utils.h>

namespace libfreenect2
{
namespace recording
{

bool loadRecordingMetadata(const std::string& directory, bool salvage_incomplete,
                           RecordingMetadata& metadata, std::string* error)
{
  if (!directoryExists(directory))
  {
    if (error != 0)
      *error = "recording directory does not exist: '" + directory + "'";
    return false;
  }

  size_t complete_size = 0;
  if (!regularFileSize(joinPath(directory, "recording.complete"), complete_size) ||
      complete_size == 0)
  {
    if (!salvage_incomplete)
    {
      if (error != 0)
        *error = "recording is incomplete; enable salvage mode to inspect it";
      return false;
    }
  }

  std::vector<unsigned char> manifest_bytes;
  std::string local_error;
  if (!readFile(joinPath(directory, "manifest.json"), manifest_bytes, &local_error))
  {
    if (error != 0)
      *error = local_error;
    return false;
  }

  RecordingMetadata loaded;
  loaded.directory = directory;
  if (!parseManifestV1(std::string(manifest_bytes.begin(), manifest_bytes.end()), loaded.manifest,
                       &local_error))
  {
    if (error != 0)
      *error = local_error;
    return false;
  }

  loaded.calibration.color = loaded.manifest.color;
  loaded.calibration.ir = loaded.manifest.ir;
  if (!readFile(joinPath(directory, loaded.manifest.p0_path), loaded.calibration.p0_tables,
                &local_error))
  {
    if (error != 0)
      *error = local_error;
    return false;
  }
  if (loaded.calibration.p0_tables.size() != sizeof(protocol::P0TablesResponse))
  {
    if (error != 0)
      *error = "recording calibration contains an invalid P0 table length";
    return false;
  }

  metadata = loaded;
  return true;
}

} // namespace recording
} // namespace libfreenect2
