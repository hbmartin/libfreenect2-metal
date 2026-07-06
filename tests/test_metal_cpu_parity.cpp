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

/** @file test_metal_cpu_parity.cpp The headline guardrail for this fork: the
 * Metal GPU depth processor must agree with the reference CPU processor on the
 * same synthetic input. Requires a real Metal device (any Mac GPU); no Kinect.
 * Skips gracefully when Metal is unavailable. Tests live in the "MetalCpuParity"
 * suite so CI can label them "gpu". */

#include <cmath>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include <libfreenect2/config.h>
#include <libfreenect2/depth_packet_processor.h>
#include <libfreenect2/frame_listener.hpp>
#include <libfreenect2/libfreenect2.hpp>

#include "support/synthetic.h"

using libfreenect2::CpuDepthPacketProcessor;
using libfreenect2::DepthPacket;
using libfreenect2::DepthPacketProcessor;
using libfreenect2::Frame;
using libfreenect2::Freenect2Device;
using libfreenect2::testing::CollectingFrameListener;
using libfreenect2::testing::loadSyntheticTables;
using libfreenect2::testing::makeIrParams;
using libfreenect2::testing::makeSyntheticDepthBuffer;

#ifdef LIBFREENECT2_WITH_METAL_SUPPORT

namespace
{

const int kW = 512;
const int kH = 424;

bool depthInvalid(float v)
{
  return std::isnan(v) || v <= 0.0f;
}

struct Agreement
{
  double depth_ratio;
  double ir_ratio;
};

// Fraction of pixels on which two depth/ir frame pairs agree. A depth pixel
// agrees if both are invalid or within `depth_tol_mm`; an ir pixel agrees if
// within a relative tolerance. The unwrapping step is mildly sensitive to
// float-op ordering, so a small number of boundary pixels are allowed to
// differ — a genuinely broken port disagrees almost everywhere.
Agreement compare(const Frame* depth_a, const Frame* depth_b, const Frame* ir_a, const Frame* ir_b,
                  float depth_tol_mm)
{
  const float* da = reinterpret_cast<const float*>(depth_a->data);
  const float* db = reinterpret_cast<const float*>(depth_b->data);
  const float* ia = reinterpret_cast<const float*>(ir_a->data);
  const float* ib = reinterpret_cast<const float*>(ir_b->data);

  int depth_ok = 0, ir_ok = 0;
  const int n = kW * kH;
  for (int i = 0; i < n; ++i)
  {
    if (depthInvalid(da[i]) && depthInvalid(db[i]))
      ++depth_ok;
    else if (!depthInvalid(da[i]) && !depthInvalid(db[i]) &&
             std::fabs(da[i] - db[i]) <= depth_tol_mm)
      ++depth_ok;

    float mag = std::fmax(std::fabs(ia[i]), std::fabs(ib[i]));
    float tol = std::fmax(1.0f, 1e-2f * mag);
    if (std::fabs(ia[i] - ib[i]) <= tol)
      ++ir_ok;
  }
  Agreement a;
  a.depth_ratio = static_cast<double>(depth_ok) / n;
  a.ir_ratio = static_cast<double>(ir_ok) / n;
  return a;
}

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

// Run one processor over a synthetic packet and return its collected frames via
// the provided listener (which retains ownership).
void run(DepthPacketProcessor& proc, const Freenect2Device::Config& config,
         std::vector<unsigned char>& buffer, CollectingFrameListener& listener)
{
  proc.setConfiguration(config);
  loadSyntheticTables(proc, makeIrParams());
  proc.setFrameListener(&listener);
  DepthPacket packet = makePacket(buffer);
  proc.process(packet);
}

} // namespace

TEST(MetalCpuParity, IrAndDepthMatchReference)
{
  libfreenect2::MetalDepthPacketProcessor metal;
  if (!metal.good())
    GTEST_SKIP() << "No usable Metal device on this machine";

  std::vector<unsigned char> buffer = makeSyntheticDepthBuffer(3);
  Freenect2Device::Config config; // filters on

  CollectingFrameListener cpu_out, metal_out;
  CpuDepthPacketProcessor cpu;
  run(cpu, config, buffer, cpu_out);
  run(metal, config, buffer, metal_out);

  ASSERT_NE(cpu_out.depth(), nullptr);
  ASSERT_NE(metal_out.depth(), nullptr);

  Agreement a = compare(cpu_out.depth(), metal_out.depth(), cpu_out.ir(), metal_out.ir(),
                        /*depth_tol_mm=*/1.0f);

  EXPECT_GT(a.ir_ratio, 0.99) << "Metal IR diverges from CPU reference";
  EXPECT_GT(a.depth_ratio, 0.95) << "Metal depth diverges from CPU reference";
}

TEST(MetalCpuParity, ReconfigurationIsAdoptedCleanly)
{
  // Guards the config-race fix (commit 72a12d7): a mid-stream setConfiguration
  // must be adopted atomically at frame top, so the produced frame matches a
  // CPU processor freshly configured the same way — no stale-stage garbage.
  libfreenect2::MetalDepthPacketProcessor metal;
  if (!metal.good())
    GTEST_SKIP() << "No usable Metal device on this machine";

  std::vector<unsigned char> buffer = makeSyntheticDepthBuffer(5);

  Freenect2Device::Config filters_on;
  Freenect2Device::Config filters_off;
  filters_off.EnableBilateralFilter = false;
  filters_off.EnableEdgeAwareFilter = false;

  // Metal: process once with filters on, then reconfigure to off and reprocess.
  metal.setConfiguration(filters_on);
  loadSyntheticTables(metal, makeIrParams());
  {
    CollectingFrameListener warmup;
    metal.setFrameListener(&warmup);
    DepthPacket p0 = makePacket(buffer);
    metal.process(p0);
  }
  metal.setConfiguration(filters_off);
  CollectingFrameListener metal_out;
  metal.setFrameListener(&metal_out);
  DepthPacket p1 = makePacket(buffer);
  metal.process(p1);

  // CPU reference configured with filters off from the start.
  CollectingFrameListener cpu_out;
  CpuDepthPacketProcessor cpu;
  run(cpu, filters_off, buffer, cpu_out);

  ASSERT_NE(metal_out.depth(), nullptr);
  ASSERT_NE(cpu_out.depth(), nullptr);

  Agreement a = compare(cpu_out.depth(), metal_out.depth(), cpu_out.ir(), metal_out.ir(),
                        /*depth_tol_mm=*/1.0f);

  EXPECT_GT(a.ir_ratio, 0.99) << "Post-reconfig Metal IR diverges from CPU";
  EXPECT_GT(a.depth_ratio, 0.95) << "Post-reconfig Metal depth diverges from CPU";
}

#else // !LIBFREENECT2_WITH_METAL_SUPPORT

TEST(MetalCpuParity, IrAndDepthMatchReference)
{
  GTEST_SKIP() << "Built without Metal support (LIBFREENECT2_WITH_METAL_SUPPORT)";
}

TEST(MetalCpuParity, ReconfigurationIsAdoptedCleanly)
{
  GTEST_SKIP() << "Built without Metal support (LIBFREENECT2_WITH_METAL_SUPPORT)";
}

#endif // LIBFREENECT2_WITH_METAL_SUPPORT
