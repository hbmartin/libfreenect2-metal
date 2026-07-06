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

#include "support/synthetic.h"

#include <cmath>
#include <cstring>

#include <libfreenect2/protocol/response.h>

namespace libfreenect2
{
namespace testing
{

namespace
{
// Small deterministic LCG so fixtures are reproducible across runs/platforms.
struct Lcg
{
  uint32_t state;
  explicit Lcg(uint32_t seed) : state(seed ? seed : 1u) {}
  uint32_t next()
  {
    state = state * 1664525u + 1013904223u;
    return state;
  }
};

const int kWidth = 512;
const int kHeight = 424;
const size_t kTableSize = libfreenect2::DepthPacketProcessor::TABLE_SIZE; // 512*424
const size_t kLutSize = libfreenect2::DepthPacketProcessor::LUT_SIZE;     // 2048
} // namespace

CollectingFrameListener::CollectingFrameListener()
    : ir_(0), depth_(0), ir_count_(0), depth_count_(0)
{
}

CollectingFrameListener::~CollectingFrameListener()
{
  reset();
}

void CollectingFrameListener::reset()
{
  delete ir_;
  delete depth_;
  ir_ = 0;
  depth_ = 0;
  ir_count_ = 0;
  depth_count_ = 0;
}

bool CollectingFrameListener::onNewFrame(libfreenect2::Frame::Type type, libfreenect2::Frame* frame)
{
  if (type == libfreenect2::Frame::Ir)
  {
    delete ir_;
    ir_ = frame;
    ++ir_count_;
  }
  else if (type == libfreenect2::Frame::Depth)
  {
    delete depth_;
    depth_ = frame;
    ++depth_count_;
  }
  else
  {
    // Not a type this listener is used for; free it to avoid a leak.
    delete frame;
  }
  // Took ownership.
  return true;
}

libfreenect2::Freenect2Device::IrCameraParams makeIrParams()
{
  // Values close to typical Kinect v2 factory presets; the exact numbers only
  // need to be self-consistent and finite for the math to be exercisable.
  libfreenect2::Freenect2Device::IrCameraParams p;
  p.fx = 365.0f;
  p.fy = 365.0f;
  p.cx = 256.0f;
  p.cy = 212.0f;
  p.k1 = 0.09f;
  p.k2 = -0.27f;
  p.k3 = 0.10f;
  p.p1 = 0.0f;
  p.p2 = 0.0f;
  return p;
}

libfreenect2::Freenect2Device::ColorCameraParams makeColorParams()
{
  libfreenect2::Freenect2Device::ColorCameraParams p;
  std::memset(&p, 0, sizeof(p));
  p.fx = 1081.0f;
  p.fy = 1081.0f;
  p.cx = 959.5f;
  p.cy = 539.5f;
  p.shift_d = 863.0f;
  p.shift_m = 52.0f;
  // A near-identity depth->color polynomial: color x ~= depth x, y ~= y.
  p.mx_x1y0 = 1.0f; // x coefficient
  p.my_x0y1 = 1.0f; // y coefficient
  return p;
}

std::vector<unsigned char> makeSyntheticP0Tables(uint32_t seed)
{
  std::vector<unsigned char> buf(sizeof(libfreenect2::protocol::P0TablesResponse), 0);
  libfreenect2::protocol::P0TablesResponse* resp =
      reinterpret_cast<libfreenect2::protocol::P0TablesResponse*>(buf.data());

  resp->headersize = sizeof(libfreenect2::protocol::P0TablesResponse);
  resp->tablesize = static_cast<uint32_t>(kTableSize * sizeof(uint16_t));

  Lcg rng(seed);
  for (size_t i = 0; i < kTableSize; ++i)
  {
    resp->p0table0[i] = static_cast<uint16_t>(rng.next() & 0xffff);
    resp->p0table1[i] = static_cast<uint16_t>(rng.next() & 0xffff);
    resp->p0table2[i] = static_cast<uint16_t>(rng.next() & 0xffff);
  }
  return buf;
}

void makeSyntheticXZTables(const libfreenect2::Freenect2Device::IrCameraParams& ir,
                           std::vector<float>& xtable, std::vector<float>& ztable)
{
  xtable.assign(kTableSize, 0.0f);
  ztable.assign(kTableSize, 0.0f);
  for (int y = 0; y < kHeight; ++y)
  {
    for (int x = 0; x < kWidth; ++x)
    {
      size_t i = static_cast<size_t>(y) * kWidth + x;
      // Normalized ray directions, like the real back-projection tables.
      xtable[i] = (static_cast<float>(x) - ir.cx) / ir.fx;
      ztable[i] = (static_cast<float>(y) - ir.cy) / ir.fy;
    }
  }
}

void makeSyntheticLookupTable(std::vector<short>& lut)
{
  lut.assign(kLutSize, 0);
  for (size_t i = 0; i < kLutSize; ++i)
  {
    // Gentle monotonic ramp, kept well under the 32767 saturation sentinel.
    lut[i] = static_cast<short>(i * 8);
  }
}

std::vector<unsigned char> makeSyntheticDepthBuffer(uint32_t seed)
{
  const size_t single_image = static_cast<size_t>(kWidth) * kHeight * 11 / 8;
  std::vector<unsigned char> buf(10 * single_image);
  Lcg rng(seed);
  for (size_t i = 0; i < buf.size(); ++i)
    buf[i] = static_cast<unsigned char>(rng.next() & 0xff);
  return buf;
}

void loadSyntheticTables(libfreenect2::DepthPacketProcessor& proc,
                         const libfreenect2::Freenect2Device::IrCameraParams& ir, uint32_t seed)
{
  std::vector<unsigned char> p0 = makeSyntheticP0Tables(seed);
  proc.loadP0TablesFromCommandResponse(p0.data(), p0.size());

  std::vector<float> xtable, ztable;
  makeSyntheticXZTables(ir, xtable, ztable);
  proc.loadXZTables(xtable.data(), ztable.data());

  std::vector<short> lut;
  makeSyntheticLookupTable(lut);
  proc.loadLookupTable(lut.data());
}

} // namespace testing
} // namespace libfreenect2
