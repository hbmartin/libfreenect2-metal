/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#include <libfreenect2/recording_journal.h>
#include <libfreenect2/recording_utils.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <exception>
#include <limits>

namespace libfreenect2
{
namespace recording
{
namespace
{

typedef nlohmann::json Json;

bool validateJournalEntry(const JournalEntry& entry, std::string* error)
{
  if (entry.stream != "color" && entry.stream != "depth")
  {
    if (error != 0)
      *error = "frame journal entry contains an unsupported stream";
    return false;
  }
  if (!isSafeRelativePath(entry.path))
  {
    if (error != 0)
      *error = "frame journal entry contains an unsafe path";
    return false;
  }
  if (entry.byte_count == 0)
  {
    if (error != 0)
      *error = "frame journal entry contains an empty frame";
    return false;
  }
  if ((entry.stream == "color") != entry.has_rgb_metadata)
  {
    if (error != 0)
      *error = "frame journal RGB metadata does not match the stream";
    return false;
  }
  if (entry.has_rgb_metadata &&
      (!std::isfinite(entry.exposure) || !std::isfinite(entry.gain) || !std::isfinite(entry.gamma)))
  {
    if (error != 0)
      *error = "frame journal contains non-finite RGB metadata";
    return false;
  }
  return true;
}

} // namespace

JournalEntry::JournalEntry()
    : index(0), byte_count(0), device_timestamp(0), sequence(0), arrival_offset_us(0),
      has_rgb_metadata(false), exposure(0.0f), gain(0.0f), gamma(0.0f)
{
}

bool serializeJournalEntry(const JournalEntry& entry, std::string& line, std::string* error)
{
  if (!validateJournalEntry(entry, error))
    return false;

  Json value = {{"index", entry.index},
                {"stream", entry.stream},
                {"path", entry.path},
                {"byte_count", entry.byte_count},
                {"device_timestamp", entry.device_timestamp},
                {"sequence", entry.sequence},
                {"arrival_offset_us", entry.arrival_offset_us}};
  if (entry.has_rgb_metadata)
  {
    value["exposure"] = entry.exposure;
    value["gain"] = entry.gain;
    value["gamma"] = entry.gamma;
  }
  line = value.dump() + "\n";
  return true;
}

bool parseJournalEntry(const std::string& line, JournalEntry& entry, std::string* error)
{
  try
  {
    const Json value = Json::parse(line);
    JournalEntry parsed;
    parsed.index = value.at("index").get<uint64_t>();
    parsed.stream = value.at("stream").get<std::string>();
    parsed.path = value.at("path").get<std::string>();
    const uint64_t byte_count = value.at("byte_count").get<uint64_t>();
    if (byte_count > std::numeric_limits<size_t>::max())
      throw std::range_error("byte count is not representable");
    parsed.byte_count = static_cast<size_t>(byte_count);
    parsed.device_timestamp = value.at("device_timestamp").get<uint32_t>();
    parsed.sequence = value.at("sequence").get<uint32_t>();
    parsed.arrival_offset_us = value.at("arrival_offset_us").get<uint64_t>();
    parsed.has_rgb_metadata = parsed.stream == "color";
    if (parsed.has_rgb_metadata)
    {
      parsed.exposure = value.at("exposure").get<float>();
      parsed.gain = value.at("gain").get<float>();
      parsed.gamma = value.at("gamma").get<float>();
    }
    if (!validateJournalEntry(parsed, error))
      return false;
    entry = parsed;
    return true;
  }
  catch (const std::exception& exception)
  {
    if (error != 0)
      *error = std::string("invalid frame journal entry: ") + exception.what();
    return false;
  }
}

FrameJournal::FrameJournal() : next_index_(0) {}

FrameJournal::~FrameJournal()
{
  close();
}

bool FrameJournal::failLocked(const std::string& message, std::string* error)
{
  last_error_ = message;
  if (error != 0)
    *error = message;
  return false;
}

bool FrameJournal::open(const std::string& path, std::string* error)
{
  libfreenect2::lock_guard guard(mutex_);
  if (stream_.is_open())
    return failLocked("frame journal is already open", error);
  stream_.open(path.c_str(), std::ios::binary | std::ios::trunc);
  if (!stream_)
    return failLocked("failed to open frame journal '" + path + "'", error);
  next_index_ = 0;
  last_error_.clear();
  return true;
}

bool FrameJournal::append(const JournalEntry& entry, std::string* error)
{
  libfreenect2::lock_guard guard(mutex_);
  if (!stream_.is_open())
    return failLocked("frame journal is not open", error);
  if (entry.index != next_index_)
    return failLocked("frame journal index is not contiguous", error);

  std::string line;
  std::string validation_error;
  if (!serializeJournalEntry(entry, line, &validation_error))
    return failLocked(validation_error, error);
  stream_.write(line.data(), static_cast<std::streamsize>(line.size()));
  stream_.flush();
  if (!stream_)
    return failLocked("failed to append the frame journal", error);
  ++next_index_;
  return true;
}

bool FrameJournal::close(std::string* error)
{
  libfreenect2::lock_guard guard(mutex_);
  if (!stream_.is_open())
    return last_error_.empty() || failLocked(last_error_, error);
  stream_.flush();
  const bool success = stream_.good();
  stream_.close();
  if (!success)
    return failLocked("failed to close the frame journal cleanly", error);
  return true;
}

std::string FrameJournal::getLastError() const
{
  libfreenect2::lock_guard guard(mutex_);
  return last_error_;
}

} // namespace recording
} // namespace libfreenect2
