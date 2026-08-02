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

class RgbDecoderFallback::DeferredFrameListener : public FrameListener
{
public:
  DeferredFrameListener() : target_(0) {}
  virtual ~DeferredFrameListener() { discard(); }

  virtual bool onNewFrame(Frame::Type type, Frame* frame)
  {
    frames_.push_back(PendingFrame(type, frame));
    return true;
  }

  void setTarget(FrameListener* target) { target_ = target; }

  void publish()
  {
    std::vector<PendingFrame> pending;
    pending.swap(frames_);
    for (std::vector<PendingFrame>::iterator frame = pending.begin(); frame != pending.end();
         ++frame)
    {
      if (target_ == 0 || !target_->onNewFrame(frame->type, frame->frame))
        delete frame->frame;
    }
    frames_.clear();
  }

  void discard()
  {
    std::vector<PendingFrame> pending;
    pending.swap(frames_);
    for (std::vector<PendingFrame>::iterator frame = pending.begin(); frame != pending.end();
         ++frame)
      delete frame->frame;
  }

private:
  struct PendingFrame
  {
    PendingFrame(Frame::Type type_, Frame* frame_) : type(type_), frame(frame_) {}
    Frame::Type type;
    Frame* frame;
  };

  FrameListener* target_;
  std::vector<PendingFrame> frames_;
};

RgbDecoderFallback::RgbDecoderFallback(RgbPacketProcessor* primary, RgbPacketProcessor* fallback)
    : primary_(primary), fallback_(fallback), primary_listener_(new DeferredFrameListener()),
      using_fallback_(primary == 0 || !primary->good()), good_(false)
{
  RgbPacketProcessor* active = using_fallback_.load() ? fallback_ : primary_;
  good_.store(active != 0 && active->good());
}

RgbDecoderFallback::~RgbDecoderFallback()
{
  delete primary_;
  delete fallback_;
  delete primary_listener_;
}

bool RgbDecoderFallback::good()
{
  return good_.load();
}

const char* RgbDecoderFallback::name()
{
  RgbPacketProcessor* active = using_fallback_ ? fallback_ : primary_;
  return active == 0 ? "Unavailable RGB decoder" : active->name();
}

void RgbDecoderFallback::setFrameListener(FrameListener* listener)
{
  RgbPacketProcessor::setFrameListener(listener);
  primary_listener_->setTarget(listener);
  if (primary_ != 0)
    primary_->setFrameListener(listener == 0 ? 0 : primary_listener_);
  if (fallback_ != 0)
    fallback_->setFrameListener(listener);
}

void RgbDecoderFallback::process(const RgbPacket& packet)
{
  if (using_fallback_)
  {
    if (fallback_ != 0)
    {
      fallback_->process(packet);
      good_.store(fallback_->good());
    }
    else
    {
      good_.store(false);
    }
    return;
  }

  // A hardware decoder may mutate or temporarily unmap the allocator-owned
  // JPEG buffer. Preserve the compressed bytes before invoking the primary
  // decoder if this packet might need retrying.
  std::vector<unsigned char> jpeg_copy;
  if (fallback_ != 0 && packet.jpeg_buffer != 0 && packet.jpeg_buffer_length != 0)
    jpeg_copy.assign(packet.jpeg_buffer, packet.jpeg_buffer + packet.jpeg_buffer_length);

  primary_listener_->discard();
  primary_->process(packet);
  if (!primary_->good())
  {
    primary_listener_->discard();
    good_.store(fallback_ != 0 && fallback_->good());
    using_fallback_ = true;
    LOG_WARNING << "primary RGB decoding failed; using the fallback for this and subsequent frames";
    if (fallback_ != 0)
    {
      RgbPacket retry = packet;
      if (!jpeg_copy.empty())
        retry.jpeg_buffer = &jpeg_copy[0];
      retry.memory = 0;
      fallback_->process(retry);
      good_.store(fallback_->good());
    }
  }
  else
  {
    primary_listener_->publish();
    good_.store(true);
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
