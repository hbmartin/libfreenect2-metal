/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#include <libfreenect2/recording_loader.h>

#include <libfreenect2/protocol/response.h>
#include <libfreenect2/recording_utils.h>

#include <set>

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
  if (loaded.calibration.p0_tables.size() < sizeof(protocol::P0TablesResponse))
  {
    if (error != 0)
      *error = "recording calibration contains a truncated P0 table response";
    return false;
  }

  metadata = loaded;
  return true;
}

bool loadRecordingData(const std::string& directory, bool salvage_incomplete,
                       RecordingData& recording, std::string* error)
{
  RecordingMetadata metadata;
  std::string local_error;
  if (!loadRecordingMetadata(directory, salvage_incomplete, metadata, &local_error))
  {
    if (error != 0)
      *error = local_error;
    return false;
  }

  std::vector<unsigned char> journal_bytes;
  if (!readFile(joinPath(directory, "frames.ndjson"), journal_bytes, &local_error))
  {
    if (error != 0)
      *error = local_error;
    return false;
  }
  std::string journal(journal_bytes.begin(), journal_bytes.end());
  if (!journal.empty() && journal[journal.size() - 1] != '\n')
  {
    if (!salvage_incomplete)
    {
      if (error != 0)
        *error = "frame journal is not newline-terminated";
      return false;
    }
    const std::string::size_type newline = journal.find_last_of('\n');
    journal.erase(newline == std::string::npos ? 0 : newline + 1);
  }

  RecordingData loaded;
  static_cast<RecordingMetadata&>(loaded) = metadata;
  std::set<std::string> paths;
  std::string::size_type begin = 0;
  uint64_t expected_index = 0;
  while (begin < journal.size())
  {
    const std::string::size_type newline = journal.find('\n', begin);
    if (newline == std::string::npos)
      break;
    const std::string line = journal.substr(begin, newline - begin);
    if (line.empty())
    {
      if (error != 0)
        *error = "frame journal contains an empty non-final entry";
      return false;
    }

    JournalEntry entry;
    if (!parseJournalEntry(line, entry, &local_error))
    {
      if (error != 0)
        *error = local_error;
      return false;
    }
    if (entry.index != expected_index++)
    {
      if (error != 0)
        *error = "frame journal indices are not contiguous";
      return false;
    }
    const bool extension_matches = (entry.stream == "color" && entry.path.size() >= 4 &&
                                    entry.path.compare(entry.path.size() - 4, 4, ".jpg") == 0) ||
                                   (entry.stream == "depth" && entry.path.size() >= 6 &&
                                    entry.path.compare(entry.path.size() - 6, 6, ".depth") == 0);
    if (!extension_matches || !paths.insert(entry.path).second)
    {
      if (error != 0)
        *error = "frame journal contains a mismatched or duplicate path";
      return false;
    }
    size_t actual_size = 0;
    if (!regularFileSize(joinPath(directory, entry.path), actual_size) ||
        actual_size != entry.byte_count)
    {
      if (error != 0)
        *error = "frame journal path or byte count does not validate: '" + entry.path + "'";
      return false;
    }
    loaded.entries.push_back(entry);
    begin = newline + 1;
  }

  recording = loaded;
  return true;
}

} // namespace recording
} // namespace libfreenect2
