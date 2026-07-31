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
  // Written by the async decode thread, read by the parser thread through
  // getAllocator() and by user threads through good()/name().
  std::atomic<bool> using_fallback_;
};

} // namespace libfreenect2

#endif // LIBFREENECT2_RGB_DECODER_FALLBACK_H_
