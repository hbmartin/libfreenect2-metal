/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#include <libfreenect2/rgb_decoder_fallback.h>

#include <libfreenect2/logging.h>

#include <vector>

namespace libfreenect2
{

RgbDecoderFallback::RgbDecoderFallback(RgbPacketProcessor* primary, RgbPacketProcessor* fallback)
    : primary_(primary), fallback_(fallback), using_fallback_(primary == 0 || !primary->good())
{
}

RgbDecoderFallback::~RgbDecoderFallback()
{
  delete primary_;
  delete fallback_;
}

bool RgbDecoderFallback::good()
{
  return using_fallback_ ? fallback_ != 0 && fallback_->good() : primary_ != 0 && primary_->good();
}

const char* RgbDecoderFallback::name()
{
  RgbPacketProcessor* active = using_fallback_ ? fallback_ : primary_;
  return active == 0 ? "Unavailable RGB decoder" : active->name();
}

void RgbDecoderFallback::setFrameListener(FrameListener* listener)
{
  RgbPacketProcessor::setFrameListener(listener);
  if (primary_ != 0)
    primary_->setFrameListener(listener);
  if (fallback_ != 0)
    fallback_->setFrameListener(listener);
}

void RgbDecoderFallback::process(const RgbPacket& packet)
{
  if (using_fallback_)
  {
    if (fallback_ != 0)
      fallback_->process(packet);
    return;
  }

  // VAAPI temporarily unmaps the allocator-owned JPEG buffer. A remap after
  // failure may return a different address, so preserve the compressed bytes
  // before invoking the primary decoder if this packet might need retrying.
  std::vector<unsigned char> jpeg_copy;
  if (fallback_ != 0 && packet.jpeg_buffer != 0 && packet.jpeg_buffer_length != 0)
    jpeg_copy.assign(packet.jpeg_buffer, packet.jpeg_buffer + packet.jpeg_buffer_length);

  primary_->process(packet);
  if (!primary_->good())
  {
    using_fallback_ = true;
    LOG_WARNING << "primary RGB decoding failed; using the fallback for this and subsequent frames";
    if (fallback_ != 0)
    {
      RgbPacket retry = packet;
      if (!jpeg_copy.empty())
        retry.jpeg_buffer = &jpeg_copy[0];
      retry.memory = 0;
      fallback_->process(retry);
    }
  }
}

void RgbDecoderFallback::releaseBuffer(RgbPacket& packet)
{
  if (packet.memory != 0 && packet.memory->allocator != 0)
    packet.memory->allocator->free(packet.memory);
  packet.memory = 0;
}

bool RgbDecoderFallback::usingFallback() const
{
  return using_fallback_;
}

Allocator* RgbDecoderFallback::getAllocator()
{
  RgbPacketProcessor* active = using_fallback_ ? fallback_ : primary_;
  return active == 0 ? 0 : active->getPacketAllocator();
}

} // namespace libfreenect2
