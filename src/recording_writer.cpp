/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#include <libfreenect2/recording.h>

#include <libfreenect2/recording_journal.h>
#include <libfreenect2/recording_utils.h>
#include <libfreenect2/threading.h>
#include <libfreenect2/timing.h>

#include <cstring>
#include <deque>
#include <iomanip>
#include <limits>
#include <sstream>
#include <vector>

namespace libfreenect2
{
namespace
{

struct RecordingJob
{
  recording::JournalEntry entry;
  std::vector<unsigned char> data;
};

bool frameByteCount(const Frame& frame, size_t& byte_count)
{
  if (frame.width == 0 || frame.height == 0 || frame.bytes_per_pixel == 0)
    return false;
  if (frame.width > std::numeric_limits<size_t>::max() / frame.height)
    return false;
  const size_t pixels = frame.width * frame.height;
  if (pixels > std::numeric_limits<size_t>::max() / frame.bytes_per_pixel)
    return false;
  byte_count = pixels * frame.bytes_per_pixel;
  return true;
}

std::string frameRelativePath(const std::string& stream, uint64_t index, const char* extension)
{
  std::ostringstream path;
  path << "frames/" << stream << "/" << std::setfill('0') << std::setw(10) << index << extension;
  return path.str();
}

} // namespace

class RecordingWriterImpl
{
public:
  RecordingWriterImpl(const std::string& directory, size_t queue_capacity)
      : directory_(directory), queue_capacity_(queue_capacity), worker_(0), accepting_(false),
        stopping_(false), start_time_us_(monotonicTimeMicroseconds()), next_index_(0)
  {
    if (directory_.empty())
    {
      last_error_ = "recording directory is empty";
      return;
    }
    if (queue_capacity_ == 0)
    {
      last_error_ = "recording queue capacity must be positive";
      return;
    }
    if (recording::directoryExists(directory_))
    {
      last_error_ = "recording directory already exists: '" + directory_ + "'";
      return;
    }

    std::string error;
    if (!recording::createDirectories(recording::joinPath(directory_, "frames/color"), &error) ||
        !recording::createDirectories(recording::joinPath(directory_, "frames/depth"), &error) ||
        !recording::createDirectories(recording::joinPath(directory_, "calibration"), &error) ||
        !journal_.open(recording::joinPath(directory_, "frames.ndjson"), &error))
    {
      last_error_ = error;
      return;
    }

    accepting_ = true;
    worker_ = new libfreenect2::thread(&RecordingWriterImpl::run, this);
  }

  ~RecordingWriterImpl() { close(); }

  bool enqueue(Frame::Type type, Frame* frame)
  {
    if (type != Frame::Color || frame == 0 || frame->format != Frame::Raw || frame->data == 0)
      return false;

    size_t byte_count = 0;
    if (!frameByteCount(*frame, byte_count))
      return false;

    RecordingJob job;
    {
      libfreenect2::lock_guard guard(mutex_);
      if (!accepting_)
        return false;
      if (queue_.size() >= queue_capacity_)
      {
        ++stats_.dropped_frames;
        return false;
      }

      job.entry.index = next_index_++;
      job.entry.stream = "color";
      job.entry.path = frameRelativePath("color", job.entry.index, ".jpg");
      job.entry.byte_count = byte_count;
      job.entry.device_timestamp = frame->timestamp;
      job.entry.sequence = frame->sequence;
      job.entry.arrival_offset_us = frame->arrival_timestamp_us > start_time_us_
                                        ? frame->arrival_timestamp_us - start_time_us_
                                        : 0;
      job.entry.has_rgb_metadata = true;
      job.entry.exposure = frame->exposure;
      job.entry.gain = frame->gain;
      job.entry.gamma = frame->gamma;
      job.data.assign(frame->data, frame->data + byte_count);
      queue_.push_back(job);
    }
    condition_.notify_one();
    return false;
  }

  bool setCalibration(const std::string&, const std::string&, const CalibrationData&)
  {
    libfreenect2::lock_guard guard(mutex_);
    last_error_ = "recording calibration persistence is not initialized";
    return false;
  }

  bool close()
  {
    {
      libfreenect2::lock_guard guard(mutex_);
      accepting_ = false;
      stopping_ = true;
    }
    condition_.notify_all();
    if (worker_ != 0)
    {
      worker_->join();
      delete worker_;
      worker_ = 0;
    }

    std::string journal_error;
    const bool journal_closed = journal_.close(&journal_error);
    libfreenect2::lock_guard guard(mutex_);
    if (!journal_closed && last_error_.empty())
      last_error_ = journal_error;
    return last_error_.empty();
  }

  bool isOpen() const
  {
    libfreenect2::lock_guard guard(mutex_);
    return accepting_;
  }

  std::string getLastError() const
  {
    libfreenect2::lock_guard guard(mutex_);
    return last_error_;
  }

  RecordingWriter::Stats getStats() const
  {
    libfreenect2::lock_guard guard(mutex_);
    return stats_;
  }

private:
  void fail(const std::string& error)
  {
    libfreenect2::lock_guard guard(mutex_);
    if (last_error_.empty())
      last_error_ = error;
    accepting_ = false;
    stats_.dropped_frames += queue_.size();
    queue_.clear();
  }

  void run()
  {
    libfreenect2::this_thread::set_name("freenect2-record");
    for (;;)
    {
      RecordingJob job;
      {
        libfreenect2::unique_lock lock(mutex_);
        condition_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
        if (queue_.empty())
        {
          if (stopping_)
            break;
          continue;
        }
        job = queue_.front();
        queue_.pop_front();
      }

      std::string error;
      if (!recording::writeFileAtomically(recording::joinPath(directory_, job.entry.path),
                                          &job.data[0], job.data.size(), &error) ||
          !journal_.append(job.entry, &error))
      {
        fail(error);
        break;
      }

      libfreenect2::lock_guard guard(mutex_);
      ++stats_.written_frames;
      stats_.written_bytes += job.data.size();
    }
  }

  std::string directory_;
  size_t queue_capacity_;
  mutable libfreenect2::mutex mutex_;
  libfreenect2::condition_variable condition_;
  std::deque<RecordingJob> queue_;
  libfreenect2::thread* worker_;
  bool accepting_;
  bool stopping_;
  uint64_t start_time_us_;
  uint64_t next_index_;
  recording::FrameJournal journal_;
  RecordingWriter::Stats stats_;
  std::string last_error_;
};

RecordingWriter::Stats::Stats() : written_frames(0), dropped_frames(0), written_bytes(0) {}

RecordingWriter::RecordingWriter(const std::string& directory, size_t queue_capacity)
    : impl_(new RecordingWriterImpl(directory, queue_capacity))
{
}

RecordingWriter::~RecordingWriter()
{
  delete impl_;
}

bool RecordingWriter::onNewFrame(Frame::Type type, Frame* frame)
{
  return impl_->enqueue(type, frame);
}

bool RecordingWriter::setCalibration(const std::string& serial, const std::string& firmware,
                                     const CalibrationData& calibration)
{
  return impl_->setCalibration(serial, firmware, calibration);
}

bool RecordingWriter::close()
{
  return impl_->close();
}

bool RecordingWriter::isOpen() const
{
  return impl_->isOpen();
}

std::string RecordingWriter::getLastError() const
{
  return impl_->getLastError();
}

RecordingWriter::Stats RecordingWriter::getStats() const
{
  return impl_->getStats();
}

} // namespace libfreenect2
