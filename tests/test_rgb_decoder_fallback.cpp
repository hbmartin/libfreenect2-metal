/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <thread>

#include <libfreenect2/rgb_decoder_fallback.h>

namespace libfreenect2
{
namespace
{

class FakeRgbDecoder : public RgbPacketProcessor
{
public:
  FakeRgbDecoder(const char* decoder_name, bool healthy, bool fail_on_process, bool emit_frames,
                 bool mutate_packet = false)
      : decoder_name_(decoder_name), healthy_(healthy), fail_on_process_(fail_on_process),
        emit_frames_(emit_frames), mutate_packet_(mutate_packet), process_count_(0),
        last_jpeg_byte_(0)
  {
  }

  virtual bool good() { return healthy_; }
  virtual const char* name() { return decoder_name_; }

  virtual void process(const RgbPacket& packet)
  {
    ++process_count_;
    if (packet.jpeg_buffer != 0 && packet.jpeg_buffer_length != 0)
    {
      last_jpeg_byte_ = packet.jpeg_buffer[0];
      if (mutate_packet_)
        packet.jpeg_buffer[0] = 0xff;
    }
    if (fail_on_process_)
    {
      healthy_ = false;
      return;
    }
    if (emit_frames_ && listener_ != 0)
    {
      Frame* frame = new Frame(1, 1, 4);
      frame->timestamp = packet.timestamp;
      frame->sequence = packet.sequence;
      if (!listener_->onNewFrame(Frame::Color, frame))
        delete frame;
    }
  }

  int processCount() const { return process_count_; }
  unsigned char lastJpegByte() const { return last_jpeg_byte_; }

private:
  const char* decoder_name_;
  bool healthy_;
  bool fail_on_process_;
  bool emit_frames_;
  bool mutate_packet_;
  int process_count_;
  unsigned char last_jpeg_byte_;
};

class CountingColorListener : public FrameListener
{
public:
  CountingColorListener() : count_(0), last_timestamp_(0) {}

  virtual bool onNewFrame(Frame::Type type, Frame* frame)
  {
    if (type == Frame::Color)
    {
      ++count_;
      last_timestamp_ = frame->timestamp;
    }
    delete frame;
    return true;
  }

  int count_;
  uint32_t last_timestamp_;
};

class ConcurrentHealthRgbDecoder : public RgbPacketProcessor
{
public:
  ConcurrentHealthRgbDecoder() : healthy_(true), processing_(false), finish_(false) {}

  virtual bool good() { return healthy_; }

  virtual void process(const RgbPacket&)
  {
    processing_.store(true);
    while (!finish_.load())
    {
      healthy_ = !healthy_;
      std::this_thread::yield();
    }
    healthy_ = false;
  }

  bool processing() const { return processing_.load(); }
  void finish() { finish_.store(true); }

private:
  volatile bool healthy_;
  std::atomic<bool> processing_;
  std::atomic<bool> finish_;
};

RgbPacket samplePacket(uint32_t timestamp)
{
  RgbPacket packet = {};
  packet.timestamp = timestamp;
  packet.sequence = timestamp + 1;
  return packet;
}

TEST(RgbDecoderFallback, UsesFallbackAfterInitializationFailure)
{
  FakeRgbDecoder* primary = new FakeRgbDecoder("primary", false, false, false);
  FakeRgbDecoder* fallback = new FakeRgbDecoder("fallback", true, false, true);
  RgbDecoderFallback decoder(primary, fallback);
  CountingColorListener listener;
  decoder.setFrameListener(&listener);

  EXPECT_TRUE(decoder.usingFallback());
  decoder.process(samplePacket(10));
  EXPECT_EQ(0, primary->processCount());
  EXPECT_EQ(1, fallback->processCount());
  EXPECT_EQ(1, listener.count_);
}

TEST(RgbDecoderFallback, RetriesTheFailedPacketOnceThenStaysOnFallback)
{
  FakeRgbDecoder* primary = new FakeRgbDecoder("primary", true, true, false);
  FakeRgbDecoder* fallback = new FakeRgbDecoder("fallback", true, false, true);
  RgbDecoderFallback decoder(primary, fallback);
  CountingColorListener listener;
  decoder.setFrameListener(&listener);

  decoder.process(samplePacket(20));
  EXPECT_TRUE(decoder.usingFallback());
  EXPECT_EQ(1, primary->processCount());
  EXPECT_EQ(1, fallback->processCount());
  EXPECT_EQ(1, listener.count_);
  EXPECT_EQ(20u, listener.last_timestamp_);

  decoder.process(samplePacket(30));
  EXPECT_EQ(1, primary->processCount());
  EXPECT_EQ(2, fallback->processCount());
  EXPECT_EQ(2, listener.count_);
  EXPECT_EQ(30u, listener.last_timestamp_);
}

TEST(RgbDecoderFallback, RetriesFromAStableCopyOfTheCompressedPacket)
{
  FakeRgbDecoder* primary = new FakeRgbDecoder("primary", true, true, false, true);
  FakeRgbDecoder* fallback = new FakeRgbDecoder("fallback", true, false, false);
  RgbDecoderFallback decoder(primary, fallback);

  unsigned char jpeg[] = {0x2a, 0x43};
  RgbPacket packet = samplePacket(40);
  packet.jpeg_buffer = jpeg;
  packet.jpeg_buffer_length = sizeof(jpeg);
  decoder.process(packet);

  EXPECT_TRUE(decoder.usingFallback());
  EXPECT_EQ(0x2a, fallback->lastJpegByte());
  EXPECT_EQ(0xff, jpeg[0]);
}

TEST(RgbDecoderFallback, PublishesHealthWithoutReadingPrimaryAcrossThreads)
{
  ConcurrentHealthRgbDecoder* primary = new ConcurrentHealthRgbDecoder();
  FakeRgbDecoder* fallback = new FakeRgbDecoder("fallback", true, false, false);
  RgbDecoderFallback decoder(primary, fallback);
  RgbPacket packet = samplePacket(50);

  std::thread worker([&decoder, &packet]() { decoder.process(packet); });
  while (!primary->processing())
    std::this_thread::yield();

  bool remained_good = true;
  for (int i = 0; i < 1000; ++i)
    remained_good = decoder.good() && remained_good;

  primary->finish();
  worker.join();

  EXPECT_TRUE(remained_good);
  EXPECT_TRUE(decoder.usingFallback());
  EXPECT_TRUE(decoder.good());
  EXPECT_EQ(1, fallback->processCount());
}

} // namespace
} // namespace libfreenect2
