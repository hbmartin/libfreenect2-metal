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

/** @file synthetic.h Hardware-free test fixtures for the unit test suite.
 *
 * None of these helpers touch a Kinect, USB, or a GPU. They fabricate
 * deterministic tables and buffers that are structurally valid enough to drive
 * the depth pipeline so the pure-computation code can be exercised in CI.
 */

#ifndef LIBFREENECT2_TESTS_SYNTHETIC_H_
#define LIBFREENECT2_TESTS_SYNTHETIC_H_

#include <cstdint>
#include <vector>

#include <libfreenect2/frame_listener.hpp>
#include <libfreenect2/libfreenect2.hpp>
#include <libfreenect2/depth_packet_processor.h>

namespace libfreenect2
{
namespace testing
{

/** A FrameListener that keeps every frame it receives so tests can assert on
 * them. Takes ownership of the frames (returns true from onNewFrame) and frees
 * them on destruction. */
class CollectingFrameListener : public libfreenect2::FrameListener
{
public:
  CollectingFrameListener();
  ~CollectingFrameListener() override;

  bool onNewFrame(libfreenect2::Frame::Type type, libfreenect2::Frame* frame) override;

  libfreenect2::Frame* ir() const { return ir_; }
  libfreenect2::Frame* depth() const { return depth_; }
  int irCount() const { return ir_count_; }
  int depthCount() const { return depth_count_; }

  /** Discard/free any held frames and reset the counters. */
  void reset();

private:
  libfreenect2::Frame* ir_;
  libfreenect2::Frame* depth_;
  int ir_count_;
  int depth_count_;
};

/** Plausible factory-style IR (depth) camera intrinsics. */
libfreenect2::Freenect2Device::IrCameraParams makeIrParams();

/** Plausible factory-style color camera intrinsics/extrinsics. */
libfreenect2::Freenect2Device::ColorCameraParams makeColorParams();

/** Build a P0-tables command-response blob (as loadP0TablesFromCommandResponse
 * expects) filled deterministically from @p seed. Sized to hold a full
 * P0TablesResponse. */
std::vector<unsigned char> makeSyntheticP0Tables(uint32_t seed = 1);

/** Fill X and Z back-projection tables (TABLE_SIZE entries each) with
 * deterministic, finite values derived from @p ir. */
void makeSyntheticXZTables(const libfreenect2::Freenect2Device::IrCameraParams& ir,
                           std::vector<float>& xtable, std::vector<float>& ztable);

/** Fill an 11-to-16 lookup table (LUT_SIZE entries) with a deterministic
 * monotonic ramp. */
void makeSyntheticLookupTable(std::vector<short>& lut);

/** Raw depth buffer sized exactly to one assembled depth frame
 * (10 * 512*424*11/8 bytes), filled deterministically from @p seed. */
std::vector<unsigned char> makeSyntheticDepthBuffer(uint32_t seed = 1);

/** Load synthetic P0/XZ/LUT tables into any DepthPacketProcessor. */
void loadSyntheticTables(libfreenect2::DepthPacketProcessor& proc,
                         const libfreenect2::Freenect2Device::IrCameraParams& ir,
                         uint32_t seed = 1);

} // namespace testing
} // namespace libfreenect2

#endif // LIBFREENECT2_TESTS_SYNTHETIC_H_
