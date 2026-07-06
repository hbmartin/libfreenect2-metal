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

#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include <libfreenect2/depth_packet_processor.h>
#include <libfreenect2/frame_listener.hpp>
#include <libfreenect2/libfreenect2.hpp>

#include "support/synthetic.h"

using libfreenect2::CpuDepthPacketProcessor;
using libfreenect2::DepthPacket;
using libfreenect2::Frame;
using libfreenect2::Freenect2Device;
using libfreenect2::testing::CollectingFrameListener;
using libfreenect2::testing::loadSyntheticTables;
using libfreenect2::testing::makeIrParams;
using libfreenect2::testing::makeSyntheticDepthBuffer;

namespace
{

DepthPacket makePacket(std::vector<unsigned char>& buffer)
{
  DepthPacket p;
  p.sequence = 1;
  p.timestamp = 100;
  p.buffer = buffer.data();
  p.buffer_length = buffer.size();
  p.memory = 0;
  return p;
}

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
