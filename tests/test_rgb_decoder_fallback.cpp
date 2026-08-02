/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
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
        last_jpeg_byte_(0), last_sequence_(0), last_timestamp_(0), last_arrival_timestamp_us_(0),
        last_exposure_(0.0f), last_gain_(0.0f), last_gamma_(0.0f)
  {
  }

  virtual bool good() { return healthy_; }
  virtual const char* name() { return decoder_name_; }

  virtual void process(const RgbPacket& packet)
  {
    ++process_count_;
    last_sequence_ = packet.sequence;
    last_timestamp_ = packet.timestamp;
    last_arrival_timestamp_us_ = packet.arrival_timestamp_us;
    last_exposure_ = packet.exposure;
    last_gain_ = packet.gain;
    last_gamma_ = packet.gamma;
    if (packet.jpeg_buffer != 0 && packet.jpeg_buffer_length != 0)
    {
      last_jpeg_byte_ = packet.jpeg_buffer[0];
      if (mutate_packet_)
        packet.jpeg_buffer[0] = 0xff;
    }
    if (emit_frames_ && listener_ != 0)
    {
      Frame* frame = new Frame(1, 1, 4);
      frame->timestamp = packet.timestamp;
      frame->sequence = packet.sequence;
      if (!listener_->onNewFrame(Frame::Color, frame))
        delete frame;
    }
    if (fail_on_process_)
      healthy_ = false;
  }

  int processCount() const { return process_count_; }
  unsigned char lastJpegByte() const { return last_jpeg_byte_; }
  uint32_t lastSequence() const { return last_sequence_; }
  uint32_t lastTimestamp() const { return last_timestamp_; }
  uint64_t lastArrivalTimestampUs() const { return last_arrival_timestamp_us_; }
  float lastExposure() const { return last_exposure_; }
  float lastGain() const { return last_gain_; }
  float lastGamma() const { return last_gamma_; }

private:
  const char* decoder_name_;
  bool healthy_;
  bool fail_on_process_;
  bool emit_frames_;
  bool mutate_packet_;
  int process_count_;
  unsigned char last_jpeg_byte_;
  uint32_t last_sequence_;
  uint32_t last_timestamp_;
  uint64_t last_arrival_timestamp_us_;
  float last_exposure_;
  float last_gain_;
  float last_gamma_;
};

class CountingColorListener : public FrameListener
{
public:
  CountingColorListener() : count_(0), last_timestamp_(0), last_sequence_(0) {}

  virtual bool onNewFrame(Frame::Type type, Frame* frame)
  {
    if (type == Frame::Color)
    {
      ++count_;
      last_timestamp_ = frame->timestamp;
      last_sequence_ = frame->sequence;
    }
    delete frame;
    return true;
  }

  int count_;
  uint32_t last_timestamp_;
  uint32_t last_sequence_;
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
  std::atomic<bool> healthy_;
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

TEST(RgbDecoderFallback, PublishesHealthyPrimaryFramesAfterDecodeSucceeds)
{
  FakeRgbDecoder* primary = new FakeRgbDecoder("primary", true, false, true);
  FakeRgbDecoder* fallback = new FakeRgbDecoder("fallback", true, false, true);
  RgbDecoderFallback decoder(primary, fallback);
  CountingColorListener listener;
  decoder.setFrameListener(&listener);

  decoder.process(samplePacket(15));

  EXPECT_FALSE(decoder.usingFallback());
  EXPECT_EQ(1, primary->processCount());
  EXPECT_EQ(0, fallback->processCount());
  EXPECT_EQ(1, listener.count_);
  EXPECT_EQ(15u, listener.last_timestamp_);
  EXPECT_EQ(16u, listener.last_sequence_);
}

TEST(RgbDecoderFallback, RetriesFailedTegraPacketOnceThenStaysOnFallback)
{
  FakeRgbDecoder* primary = new FakeRgbDecoder("TegraJPEG", true, true, true, true);
  FakeRgbDecoder* fallback = new FakeRgbDecoder("fallback", true, false, true);
  RgbDecoderFallback decoder(primary, fallback);
  CountingColorListener listener;
  decoder.setFrameListener(&listener);

  unsigned char jpeg[] = {0x2a, 0x43};
  RgbPacket failed = samplePacket(20);
  failed.arrival_timestamp_us = 1234567;
  failed.exposure = 1.25f;
  failed.gain = 2.5f;
  failed.gamma = 3.75f;
  failed.jpeg_buffer = jpeg;
  failed.jpeg_buffer_length = sizeof(jpeg);
  decoder.process(failed);

  EXPECT_TRUE(decoder.usingFallback());
  EXPECT_EQ(1, primary->processCount());
  EXPECT_EQ(1, fallback->processCount());
  EXPECT_EQ(0x2a, fallback->lastJpegByte());
  EXPECT_EQ(0xff, jpeg[0]);
  EXPECT_EQ(failed.sequence, fallback->lastSequence());
  EXPECT_EQ(failed.timestamp, fallback->lastTimestamp());
  EXPECT_EQ(failed.arrival_timestamp_us, fallback->lastArrivalTimestampUs());
  EXPECT_FLOAT_EQ(failed.exposure, fallback->lastExposure());
  EXPECT_FLOAT_EQ(failed.gain, fallback->lastGain());
  EXPECT_FLOAT_EQ(failed.gamma, fallback->lastGamma());
  EXPECT_EQ(1, listener.count_);
  EXPECT_EQ(20u, listener.last_timestamp_);
  EXPECT_EQ(21u, listener.last_sequence_);

  decoder.process(samplePacket(30));
  EXPECT_EQ(1, primary->processCount());
  EXPECT_EQ(2, fallback->processCount());
  EXPECT_EQ(2, listener.count_);
  EXPECT_EQ(30u, listener.last_timestamp_);
  EXPECT_EQ(31u, listener.last_sequence_);
}

TEST(RgbDecoderFallback, PublishesHealthWithoutReadingPrimaryAcrossThreads)
{
  ConcurrentHealthRgbDecoder* primary = new ConcurrentHealthRgbDecoder();
  FakeRgbDecoder* fallback = new FakeRgbDecoder("fallback", true, false, false);
  RgbDecoderFallback decoder(primary, fallback);
  RgbPacket packet = samplePacket(50);

  std::thread worker([&decoder, &packet]() { decoder.process(packet); });
  const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!primary->processing() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::yield();
  if (!primary->processing())
  {
    primary->finish();
    worker.join();
    ADD_FAILURE() << "primary decoder did not start processing before the deadline";
    return;
  }

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
