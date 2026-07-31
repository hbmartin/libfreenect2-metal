/** @file test_concurrency.cpp Exercises the lock-based components under real threads. */

#include <atomic>
#include <chrono>
#include <future>
#include <limits>
#include <thread>

#include <gtest/gtest.h>

#include <libfreenect2/allocator.h>
#include <libfreenect2/async_packet_processor.h>
#include <libfreenect2/config.h>
#include <libfreenect2/frame_listener_impl.h>

using libfreenect2::Allocator;
using libfreenect2::AsyncPacketProcessor;
using libfreenect2::Buffer;
using libfreenect2::Frame;
using libfreenect2::FrameMap;
using libfreenect2::PacketProcessor;
using libfreenect2::PoolAllocator;
using libfreenect2::SyncMultiFrameListener;
using libfreenect2::TimestampAlignedFrameListener;

namespace
{

bool waitForValue(const std::atomic<int>& value, int expected)
{
  const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (value.load() != expected && std::chrono::steady_clock::now() < deadline)
    std::this_thread::yield();
  return value.load() == expected;
}

struct TestPacket
{
  TestPacket() : value(0), memory(NULL) {}
  int value;
  Buffer* memory;
};

class CountingProcessor : public PacketProcessor<TestPacket>
{
public:
  CountingProcessor() : processed(0), released(0), last_value(0) {}

  virtual void process(const TestPacket& packet)
  {
    last_value.store(packet.value);
    processed.fetch_add(1);
  }

  virtual void releaseBuffer(TestPacket& packet)
  {
    packet.memory = NULL;
    released.fetch_add(1);
  }

  virtual const char* name() { return "concurrency test"; }

  std::atomic<int> processed;
  std::atomic<int> released;
  std::atomic<int> last_value;
};

Frame* makeTimestampedFrame(uint32_t timestamp)
{
  Frame* frame = new Frame(1, 1, 4);
  frame->timestamp = timestamp;
  return frame;
}

} // namespace

TEST(PoolAllocator, BlocksUntilABufferIsReleased)
{
  PoolAllocator allocator;
  std::atomic<bool> entered(false);
  std::promise<Buffer*> first_promise;
  std::promise<Buffer*> second_promise;
  std::promise<Buffer*> result_promise;
  std::future<Buffer*> first_result = first_promise.get_future();
  std::future<Buffer*> second_result = second_promise.get_future();
  std::future<Buffer*> result = result_promise.get_future();
  std::thread waiter(
      [&]()
      {
        first_promise.set_value(allocator.allocate(64));
        second_promise.set_value(allocator.allocate(64));
        entered.store(true);
        result_promise.set_value(allocator.allocate(64));
      });

  Buffer* first = first_result.get();
  Buffer* second = second_result.get();
  const std::chrono::steady_clock::time_point entered_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!entered.load() && std::chrono::steady_clock::now() < entered_deadline)
    std::this_thread::yield();
  if (!entered.load())
  {
    allocator.free(first);
    allocator.free(second);
    waiter.join();
    ADD_FAILURE() << "waiter did not start the blocking allocation before the deadline";
    return;
  }
  EXPECT_EQ(result.wait_for(std::chrono::milliseconds(25)), std::future_status::timeout);

  allocator.free(first);
  std::future_status status = result.wait_for(std::chrono::seconds(2));
  if (status != std::future_status::ready)
  {
    allocator.free(second);
    waiter.join();
    ASSERT_EQ(status, std::future_status::ready);
    return;
  }

  Buffer* reused = result.get();
  waiter.join();

  EXPECT_EQ(reused, first);
  EXPECT_EQ(reused->length, 0u);
  allocator.free(reused);
  allocator.free(second);
}

TEST(AsyncPacketProcessor, ProcessesAndReleasesPacketsUnderLoad)
{
  CountingProcessor processor;
  {
    AsyncPacketProcessor<TestPacket> async(&processor);
    for (int value = 1; value <= 200; ++value)
    {
      while (!async.ready())
        std::this_thread::yield();

      TestPacket packet;
      packet.value = value;
      async.process(packet);
      ASSERT_TRUE(waitForValue(processor.processed, value));
      ASSERT_TRUE(waitForValue(processor.released, value));
    }
  }

  EXPECT_EQ(processor.processed.load(), 200);
  EXPECT_EQ(processor.released.load(), 200);
  EXPECT_EQ(processor.last_value.load(), 200);
}

TEST(SyncMultiFrameListener, CoordinatesProducerAndConsumerThreads)
{
  SyncMultiFrameListener listener(Frame::Color | Frame::Depth);
  std::thread producer(
      [&]()
      {
        EXPECT_TRUE(listener.onNewFrame(Frame::Color, new Frame(1, 1, 4)));
        EXPECT_TRUE(listener.onNewFrame(Frame::Depth, new Frame(1, 1, 4)));
      });

  FrameMap frames;
  listener.waitForNewFrame(frames);
  producer.join();

  ASSERT_EQ(frames.size(), 2u);
  EXPECT_NE(frames[Frame::Color], static_cast<Frame*>(NULL));
  EXPECT_NE(frames[Frame::Depth], static_cast<Frame*>(NULL));

  Frame* rejected = new Frame(1, 1, 4);
  EXPECT_FALSE(listener.onNewFrame(Frame::Color, rejected));
  delete rejected;

  listener.release(frames);
  EXPECT_TRUE(frames.empty());
}

#ifdef LIBFREENECT2_THREADING_STDLIB
TEST(SyncMultiFrameListener, TimedWaitExpiresWithoutFrames)
{
  SyncMultiFrameListener listener(Frame::Color);
  FrameMap frames;
  EXPECT_FALSE(listener.waitForNewFrame(frames, 10));
  EXPECT_TRUE(frames.empty());
}
#endif

TEST(TimestampAlignedFrameListener, SelectsTheSmallestQueuedTimestampSpan)
{
  TimestampAlignedFrameListener listener(Frame::Color | Frame::Depth, 5);
  EXPECT_FALSE(listener.onNewFrame(Frame::Color, NULL));
  EXPECT_TRUE(listener.onNewFrame(Frame::Color, makeTimestampedFrame(100)));
  EXPECT_TRUE(listener.onNewFrame(Frame::Color, makeTimestampedFrame(200)));
  EXPECT_TRUE(listener.onNewFrame(Frame::Depth, makeTimestampedFrame(203)));

  FrameMap frames;
  ASSERT_TRUE(listener.waitForNewFrame(frames, 20));
  ASSERT_EQ(frames.size(), 2u);
  EXPECT_EQ(frames[Frame::Color]->timestamp, 200u);
  EXPECT_EQ(frames[Frame::Depth]->timestamp, 203u);

  const TimestampAlignedFrameListener::Statistics statistics = listener.getStatistics();
  EXPECT_EQ(statistics.delivered, 1u);
  EXPECT_EQ(statistics.dropped, 1u);
  EXPECT_EQ(statistics.last_delta_ticks, 3u);
  EXPECT_EQ(statistics.maximum_delta_ticks, 3u);
  listener.release(frames);
}

TEST(TimestampAlignedFrameListener, WaitsForACombinationWithinTheThreshold)
{
  TimestampAlignedFrameListener listener(Frame::Color | Frame::Depth, 5);
  EXPECT_TRUE(listener.onNewFrame(Frame::Color, makeTimestampedFrame(100)));
  EXPECT_TRUE(listener.onNewFrame(Frame::Depth, makeTimestampedFrame(120)));

  FrameMap frames;
  EXPECT_FALSE(listener.waitForNewFrame(frames, 10));
  EXPECT_TRUE(listener.onNewFrame(Frame::Color, makeTimestampedFrame(122)));
  ASSERT_TRUE(listener.waitForNewFrame(frames, 20));
  EXPECT_EQ(frames[Frame::Color]->timestamp, 122u);
  EXPECT_EQ(frames[Frame::Depth]->timestamp, 120u);
  EXPECT_EQ(listener.getStatistics().dropped, 1u);
  listener.release(frames);
}

TEST(TimestampAlignedFrameListener, AlignsAcrossDeviceTimestampWraparound)
{
  TimestampAlignedFrameListener listener(Frame::Color | Frame::Depth, 4);
  EXPECT_TRUE(listener.onNewFrame(Frame::Color,
                                  makeTimestampedFrame(std::numeric_limits<uint32_t>::max() - 1)));
  EXPECT_TRUE(listener.onNewFrame(Frame::Depth, makeTimestampedFrame(2)));

  FrameMap frames;
  ASSERT_TRUE(listener.waitForNewFrame(frames, 20));
  EXPECT_EQ(listener.getStatistics().last_delta_ticks, 4u);
  listener.release(frames);
}

TEST(TimestampAlignedFrameListener, DropsTheOldestFrameOnQueueOverflow)
{
  TimestampAlignedFrameListener listener(Frame::Color | Frame::Depth, 2, 2);
  EXPECT_TRUE(listener.onNewFrame(Frame::Color, makeTimestampedFrame(10)));
  EXPECT_TRUE(listener.onNewFrame(Frame::Color, makeTimestampedFrame(20)));
  EXPECT_TRUE(listener.onNewFrame(Frame::Color, makeTimestampedFrame(30)));
  EXPECT_EQ(listener.getStatistics().dropped, 1u);

  EXPECT_TRUE(listener.onNewFrame(Frame::Depth, makeTimestampedFrame(31)));
  FrameMap frames;
  ASSERT_TRUE(listener.waitForNewFrame(frames, 20));
  EXPECT_EQ(frames[Frame::Color]->timestamp, 30u);
  EXPECT_EQ(listener.getStatistics().dropped, 2u);
  listener.release(frames);
}
