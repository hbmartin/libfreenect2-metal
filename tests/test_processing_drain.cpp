/** @file test_processing_drain.cpp Covers queued and active packet shutdown barriers. */

#include <atomic>
#include <chrono>
#include <future>

#include <gtest/gtest.h>

#include <libfreenect2/async_packet_processor.h>
#include <libfreenect2/packet_pipeline.h>

namespace
{

struct DrainPacket
{
  libfreenect2::Buffer* memory = nullptr;
};

enum class BlockAt
{
  None,
  WorkerStartup,
  Processing,
  BufferRelease
};

class GatedProcessor : public libfreenect2::PacketProcessor<DrainPacket>
{
public:
  explicit GatedProcessor(BlockAt block_at = BlockAt::None, bool accepted = true)
      : block_at_(block_at), accepted_(accepted), entered_(entry_.get_future()),
        allowed_(release_.get_future().share())
  {
  }

  const char* name() override
  {
    block(BlockAt::WorkerStartup);
    return "drain test";
  }

  bool good() override { return accepted_; }

  void process(const DrainPacket&) override
  {
    block(BlockAt::Processing);
    ++processed;
  }

  void releaseBuffer(DrainPacket& packet) override
  {
    block(BlockAt::BufferRelease);
    packet.memory = nullptr;
    ++released;
  }

  bool waitForEntry()
  {
    return entered_.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
  }

  void allowCompletion() { release_.set_value(); }

  std::atomic<int> processed{0};
  std::atomic<int> released{0};

private:
  void block(BlockAt point)
  {
    if (block_at_ == point)
    {
      entry_.set_value();
      allowed_.wait();
    }
  }

  BlockAt block_at_;
  bool accepted_;
  std::promise<void> entry_;
  std::promise<void> release_;
  std::future<void> entered_;
  std::shared_future<void> allowed_;
};

class ProcessingDrain : public testing::TestWithParam<BlockAt>
{
};

} // namespace

TEST_P(ProcessingDrain, WaitsUntilPacketAndBufferAreFinished)
{
  const BlockAt stage = GetParam();
  GatedProcessor processor(stage);
  libfreenect2::AsyncPacketProcessor<DrainPacket> async(&processor);

  // Holding the worker in name() leaves the packet queued before its mutex is acquired.
  if (stage == BlockAt::WorkerStartup && !processor.waitForEntry())
  {
    processor.allowCompletion();
    FAIL() << "worker did not reach its startup gate";
  }
  async.process(DrainPacket{});
  if (stage != BlockAt::WorkerStartup && !processor.waitForEntry())
  {
    processor.allowCompletion();
    FAIL() << "processor did not reach its gate";
  }

  std::promise<void> drain_started;
  std::future<void> started = drain_started.get_future();
  std::future<void> drained = std::async(std::launch::async,
                                         [&]()
                                         {
                                           drain_started.set_value();
                                           async.waitUntilIdle();
                                         });
  const std::future_status scheduled = started.wait_for(std::chrono::seconds(2));
  const std::future_status blocked = drained.wait_for(std::chrono::milliseconds(50));

  // Always release the gate before making assertions that can end the test.
  processor.allowCompletion();
  EXPECT_EQ(scheduled, std::future_status::ready);
  EXPECT_EQ(blocked, std::future_status::timeout);
  ASSERT_EQ(drained.wait_for(std::chrono::seconds(2)), std::future_status::ready);
  drained.get();
  EXPECT_EQ(processor.processed.load(), 1);
  EXPECT_EQ(processor.released.load(), 1);
}

INSTANTIATE_TEST_SUITE_P(QueuedAndActive, ProcessingDrain,
                         testing::Values(BlockAt::WorkerStartup, BlockAt::Processing,
                                         BlockAt::BufferRelease),
                         [](const testing::TestParamInfo<BlockAt>& info)
                         {
                           switch (info.param)
                           {
                           case BlockAt::WorkerStartup:
                             return "Queued";
                           case BlockAt::Processing:
                             return "Processing";
                           case BlockAt::BufferRelease:
                             return "BufferRelease";
                           default:
                             return "Idle";
                           }
                         });

TEST(ProcessingDrain, IdleProcessorNeedsNoPacket)
{
  GatedProcessor processor;
  libfreenect2::AsyncPacketProcessor<DrainPacket> async(&processor);

  async.waitUntilIdle();
  EXPECT_EQ(processor.processed.load(), 0);
  EXPECT_EQ(processor.released.load(), 0);
}

TEST(ProcessingDrain, RepeatedDrainAllowsPacketReuse)
{
  GatedProcessor processor;
  libfreenect2::AsyncPacketProcessor<DrainPacket> async(&processor);

  for (int count = 1; count <= 200; ++count)
  {
    async.process(DrainPacket{});
    async.waitUntilIdle();
    ASSERT_EQ(processor.processed.load(), count);
    ASSERT_EQ(processor.released.load(), count);
  }
}

TEST(ProcessingDrain, RejectedPacketStillReleasesItsBuffer)
{
  GatedProcessor processor(BlockAt::None, false);
  libfreenect2::AsyncPacketProcessor<DrainPacket> async(&processor);

  async.process(DrainPacket{});
  async.waitUntilIdle();
  EXPECT_EQ(processor.processed.load(), 0);
  EXPECT_EQ(processor.released.load(), 1);
}

TEST(ProcessingDrain, EmptyPipelineHasNoProcessorsToWaitFor)
{
  libfreenect2::PacketPipeline pipeline;
  pipeline.waitForProcessing();
}
