/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#ifndef LIBFREENECT2_RECORDING_JOURNAL_H_
#define LIBFREENECT2_RECORDING_JOURNAL_H_

#include <libfreenect2/threading.h>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>

namespace libfreenect2
{
namespace recording
{

struct JournalEntry
{
  JournalEntry();

  uint64_t index;
  std::string stream;
  std::string path;
  size_t byte_count;
  uint32_t device_timestamp;
  uint32_t sequence;
  uint64_t arrival_offset_us;
  bool has_rgb_metadata;
  float exposure;
  float gain;
  float gamma;
};

bool serializeJournalEntry(const JournalEntry& entry, std::string& line, std::string* error = 0);
bool parseJournalEntry(const std::string& line, JournalEntry& entry, std::string* error = 0);

class FrameJournal
{
public:
  FrameJournal();
  ~FrameJournal();

  bool open(const std::string& path, std::string* error = 0);
  bool append(const JournalEntry& entry, std::string* error = 0);
  bool close(std::string* error = 0);
  std::string getLastError() const;

private:
  bool failLocked(const std::string& message, std::string* error);

  mutable libfreenect2::mutex mutex_;
  std::ofstream stream_;
  std::string path_;
  uint64_t next_index_;
  std::string last_error_;
};

} // namespace recording
} // namespace libfreenect2

#endif // LIBFREENECT2_RECORDING_JOURNAL_H_
