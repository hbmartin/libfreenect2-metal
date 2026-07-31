/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * Copyright (c) 2014 individual OpenKinect contributors. See the CONTRIB file
 * for details.
 *
 * This code is licensed to you under the terms of the Apache License, version
 * 2.0, or, at your option, the terms of the GNU General Public License,
 * version 2.0. See the APACHE20 and GPL2 files for the text of the licenses.
 */

/** @file test_cpu_depth_processor.cpp Drives the reference CPU depth pipeline on
 * synthetic data (no Kinect). Establishes that it produces well-formed output
 * frames and is deterministic — it is also the correctness oracle for the Metal
 * port (see test_metal_cpu_parity.cpp). */

#include <algorithm>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include <libfreenect2/depth_packet_processor.h>
#include <libfreenect2/frame_listener.hpp>
#include <libfreenect2/libfreenect2.hpp>

#include "support/synthetic.h"

using libfreenect2::CpuDepthPacketProcessor;
using libfreenect2::DepthPacket;
using libfreenect2::DumpDepthPacketProcessor;
using libfreenect2::Frame;
using libfreenect2::FrameListener;
using libfreenect2::Freenect2Device;
using libfreenect2::testing::CollectingFrameListener;
using libfreenect2::testing::countValidDepthPixels;
using libfreenect2::testing::DepthFilterConfiguration;
using libfreenect2::testing::depthFilterConfigurations;
using libfreenect2::testing::loadSyntheticTables;
using libfreenect2::testing::makeIrParams;
using libfreenect2::testing::makeSyntheticDepthBuffer;
using libfreenect2::testing::makeSyntheticP0Tables;
using libfreenect2::testing::runSyntheticDepthProcessor;

namespace
{

DepthPacket makePacket(std::vector<unsigned char>& buffer)
{
  DepthPacket p;
  p.sequence = 1;
  p.timestamp = 100;
  p.arrival_timestamp_us = 123456;
  p.buffer = buffer.data();
  p.buffer_length = buffer.size();
  p.memory = 0;
  return p;
}

class IrOnlyRetainingListener : public FrameListener
{
public:
  IrOnlyRetainingListener() : ir_frame(0) {}
  ~IrOnlyRetainingListener() { delete ir_frame; }
  bool onNewFrame(Frame::Type type, Frame* frame)
  {
    if (type != Frame::Ir)
      return false;
    delete ir_frame;
    ir_frame = frame;
    return true;
  }
  Frame* ir_frame;
};

} // namespace

TEST(CpuDepthProcessor, ProducesWellFormedIrAndDepthFrames)
{
  CpuDepthPacketProcessor proc;
  Freenect2Device::Config config; // defaults: 0.5, 4.5, filters on
  proc.setConfiguration(config);
  loadSyntheticTables(proc, makeIrParams());

  CollectingFrameListener listener;
  proc.setFrameListener(&listener);

  std::vector<unsigned char> buffer = makeSyntheticDepthBuffer();
  DepthPacket packet = makePacket(buffer);
  proc.process(packet);

  ASSERT_EQ(listener.irCount(), 1);
  ASSERT_EQ(listener.depthCount(), 1);

  Frame* ir = listener.ir();
  Frame* depth = listener.depth();
  ASSERT_NE(ir, nullptr);
  ASSERT_NE(depth, nullptr);

  EXPECT_EQ(ir->width, 512u);
  EXPECT_EQ(ir->height, 424u);
  EXPECT_EQ(ir->format, Frame::Float);
  EXPECT_EQ(depth->width, 512u);
  EXPECT_EQ(depth->height, 424u);
  EXPECT_EQ(depth->format, Frame::Float);
  EXPECT_EQ(depth->timestamp, 100u);
  EXPECT_EQ(depth->arrival_timestamp_us, 123456u);
  EXPECT_EQ(depth->sequence, 1u);
}

TEST(CpuDepthProcessor, IsDeterministic)
{
  CpuDepthPacketProcessor proc;
  Freenect2Device::Config config;
  proc.setConfiguration(config);
  loadSyntheticTables(proc, makeIrParams());

  std::vector<unsigned char> buffer = makeSyntheticDepthBuffer(7);
  DepthPacket packet = makePacket(buffer);

  CollectingFrameListener a;
  proc.setFrameListener(&a);
  proc.process(packet);

  CollectingFrameListener b;
  proc.setFrameListener(&b);
  proc.process(packet);

  ASSERT_NE(a.depth(), nullptr);
  ASSERT_NE(b.depth(), nullptr);

  const size_t bytes = 512u * 424u * sizeof(float);
  EXPECT_EQ(0, std::memcmp(a.depth()->data, b.depth()->data, bytes))
      << "depth frame is not deterministic";
  EXPECT_EQ(0, std::memcmp(a.ir()->data, b.ir()->data, bytes)) << "ir frame is not deterministic";
}

TEST(CpuDepthProcessor, SupportsEveryFilterCombination)
{
  const std::array<DepthFilterConfiguration, 4>& configurations =
      depthFilterConfigurations();
  for(size_t i = 0; i < configurations.size(); ++i)
  {
    const DepthFilterConfiguration& filter = configurations[i];
    SCOPED_TRACE(filter.name);

    CpuDepthPacketProcessor first_processor;
    CpuDepthPacketProcessor second_processor;
    CollectingFrameListener first;
    CollectingFrameListener second;
    runSyntheticDepthProcessor(first_processor, filter.config(), 11, first);
    runSyntheticDepthProcessor(second_processor, filter.config(), 11, second);

    ASSERT_NE(first.depth(), nullptr);
    ASSERT_NE(second.depth(), nullptr);
    const float* depth_values = reinterpret_cast<const float*>(first.depth()->data);
    const float* ir_values = reinterpret_cast<const float*>(first.ir()->data);
    float max_depth = 0.0f;
    float max_ir = 0.0f;
    for(size_t pixel = 0; pixel < 512u * 424u; ++pixel)
    {
      max_depth = std::max(max_depth, depth_values[pixel]);
      max_ir = std::max(max_ir, ir_values[pixel]);
    }
    EXPECT_GT(countValidDepthPixels(first.depth()), 0u)
        << "max depth=" << max_depth << ", max IR=" << max_ir;

    const size_t bytes = 512u * 424u * sizeof(float);
    EXPECT_EQ(0, std::memcmp(first.depth()->data, second.depth()->data, bytes));
    EXPECT_EQ(0, std::memcmp(first.ir()->data, second.ir()->data, bytes));
  }
}

TEST(DumpDepthProcessor, IrFrameSurvivesRejectedDepthFrame)
{
  DumpDepthPacketProcessor proc;
  IrOnlyRetainingListener listener;
  proc.setFrameListener(&listener);

  std::vector<unsigned char> buffer = makeSyntheticDepthBuffer();
  DepthPacket packet = makePacket(buffer);
  proc.process(packet);

  // The listener kept the IR frame but rejected the depth frame, which the
  // processor deleted. The IR frame must own its bytes so this read is valid;
  // sanitizer builds fault here if the two frames share a buffer.
  ASSERT_NE(listener.ir_frame, nullptr);
  ASSERT_EQ(listener.ir_frame->bytes_per_pixel, buffer.size());
  EXPECT_EQ(0, std::memcmp(listener.ir_frame->data, buffer.data(), buffer.size()));
}

TEST(CpuDepthProcessor, AcceptsUnalignedP0CommandResponse)
{
  CpuDepthPacketProcessor proc;
  std::vector<unsigned char> p0 = makeSyntheticP0Tables();
  std::vector<unsigned char> unaligned(p0.size() + 1);
  std::memcpy(unaligned.data() + 1, p0.data(), p0.size());

  // USB command responses are byte buffers and do not guarantee uint16_t
  // alignment. Sanitizers exercise the decoding path for this exact layout.
  proc.loadP0TablesFromCommandResponse(unaligned.data() + 1, p0.size());
}
