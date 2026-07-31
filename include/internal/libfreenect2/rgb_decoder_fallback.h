/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#ifndef LIBFREENECT2_RGB_DECODER_FALLBACK_H_
#define LIBFREENECT2_RGB_DECODER_FALLBACK_H_

#include <libfreenect2/rgb_packet_processor.h>

#include <atomic>

namespace libfreenect2
{

class RgbDecoderFallback : public RgbPacketProcessor
{
public:
  /** Take ownership of both decoder pointers. Either pointer may be null. */
  RgbDecoderFallback(RgbPacketProcessor* primary, RgbPacketProcessor* fallback);
  virtual ~RgbDecoderFallback();

  virtual bool good();
  virtual const char* name();
  virtual void setFrameListener(FrameListener* listener);
  virtual void process(const RgbPacket& packet);
  virtual void releaseBuffer(RgbPacket& packet);

  bool usingFallback() const;

protected:
  virtual Allocator* getAllocator();

private:
  RgbPacketProcessor* primary_;
  RgbPacketProcessor* fallback_;
  // Child decoder health is read only by the decode thread and published
  // through these atomics for parser and user threads.
  std::atomic<bool> using_fallback_;
  std::atomic<bool> good_;
};

} // namespace libfreenect2

#endif // LIBFREENECT2_RGB_DECODER_FALLBACK_H_
