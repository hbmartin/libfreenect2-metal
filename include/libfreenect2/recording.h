/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#ifndef LIBFREENECT2_RECORDING_H_
#define LIBFREENECT2_RECORDING_H_

#include <libfreenect2/libfreenect2.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace libfreenect2
{

class RecordingWriterImpl;

/** Bounded, thread-safe writer for raw Kinect recording directories. */
class LIBFREENECT2_API RecordingWriter : public FrameListener
{
public:
  struct Stats
  {
    Stats();

    uint64_t written_frames;
    uint64_t written_color_frames;
    uint64_t written_depth_frames;
    uint64_t dropped_frames;
    uint64_t written_bytes;
  };

  explicit RecordingWriter(const std::string& directory, size_t queue_capacity = 32);
  virtual ~RecordingWriter();

  /** Copy a raw frame into the bounded writer queue. The caller retains ownership. */
  virtual bool onNewFrame(Frame::Type type, Frame* frame);

  /** Publish the calibration required to decode raw depth frames. */
  bool setCalibration(const std::string& serial, const std::string& firmware,
                      const CalibrationData& calibration);

  /** Flush queued frames and close the recording. Idempotent. */
  bool close();

  bool isOpen() const;
  std::string getLastError() const;
  Stats getStats() const;

private:
  RecordingWriterImpl* impl_;

  RecordingWriter(const RecordingWriter&);
  RecordingWriter& operator=(const RecordingWriter&);
};

} // namespace libfreenect2

#endif // LIBFREENECT2_RECORDING_H_
