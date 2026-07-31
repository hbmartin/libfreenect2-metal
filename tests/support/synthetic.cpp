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
const int kWidth = 512;
const int kHeight = 424;
const size_t kTableSize = libfreenect2::DepthPacketProcessor::TABLE_SIZE; // 512*424
const size_t kLutSize = libfreenect2::DepthPacketProcessor::LUT_SIZE;     // 2048
const size_t kPackedRowBytes = 352 * sizeof(uint16_t);
const size_t kPackedImageBytes = kPackedRowBytes * kHeight;

void setPackedDepthMeasurement(std::vector<unsigned char>& buffer, int subimage,
                               int x, int y, uint16_t value)
{
  const int storage_y = y < 212 ? y + 212 : 423 - y;
  const size_t bit_offset = static_cast<size_t>((x >> 2) + ((x & 3) << 7)) * 11;
  unsigned char* row = buffer.data() + subimage * kPackedImageBytes +
                       storage_y * kPackedRowBytes;
  for(size_t bit = 0; bit < 11; ++bit)
  {
    if((value & (1u << bit)) != 0)
      row[(bit_offset + bit) / 8] |=
          static_cast<unsigned char>(1u << ((bit_offset + bit) % 8));
  }
}
} // namespace

libfreenect2::Freenect2Device::Config DepthFilterConfiguration::config() const
{
  libfreenect2::Freenect2Device::Config value;
  value.EnableBilateralFilter = bilateral;
  value.EnableEdgeAwareFilter = edge_aware;
  return value;
}

const std::array<DepthFilterConfiguration, 4>& depthFilterConfigurations()
{
  static const std::array<DepthFilterConfiguration, 4> configurations = {{
      {"bilateral-and-edge", true, true},
      {"bilateral-only", true, false},
      {"edge-only", false, true},
      {"unfiltered", false, false},
  }};
  return configurations;
}

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
  (void)seed;
  return buf;
}

void makeSyntheticXZTables(const libfreenect2::Freenect2Device::IrCameraParams& ir,
                           std::vector<float>& xtable, std::vector<float>& ztable)
{
  xtable.assign(kTableSize, 0.0f);
  // The depth processor's Z table scales unwrapped phase to millimetres; it
  // is not the normalized camera-ray Z component. A constant planar scale
  // keeps the synthetic packet inside the default 0.5-4.5 m range.
  ztable.assign(kTableSize, 25000.0f);
  (void)ir;
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
  std::vector<unsigned char> buf(10 * kPackedImageBytes, 0);
  const float phase = 0.25f + 0.005f * static_cast<float>(seed % 5u);
  const float phase_step = 2.09439510239f;
  uint16_t samples[3];
  for(int sample = 0; sample < 3; ++sample)
  {
    const float raw = 1024.0f + 500.0f * std::cos(phase + sample * phase_step);
    samples[sample] = static_cast<uint16_t>(std::lround(raw));
  }

  for(int subimage = 0; subimage < 9; ++subimage)
  {
    for(int y = 0; y < kHeight; ++y)
    {
      for(int x = 0; x < kWidth; ++x)
        setPackedDepthMeasurement(buf, subimage, x, y, samples[subimage % 3]);
    }
  }
  return buf;
}

void loadSyntheticTables(libfreenect2::DepthPacketProcessor& proc,
                         const libfreenect2::Freenect2Device::IrCameraParams& ir, uint32_t seed)
{
  std::vector<unsigned char> p0 = makeSyntheticP0Tables(seed);
  std::vector<unsigned char> unaligned_p0(p0.size() + 1);
  std::memcpy(unaligned_p0.data() + 1, p0.data(), p0.size());
  proc.loadP0TablesFromCommandResponse(unaligned_p0.data() + 1, p0.size());

  std::vector<float> xtable, ztable;
  makeSyntheticXZTables(ir, xtable, ztable);
  proc.loadXZTables(xtable.data(), ztable.data());

  std::vector<short> lut;
  makeSyntheticLookupTable(lut);
  proc.loadLookupTable(lut.data());
}

void runSyntheticDepthProcessor(libfreenect2::DepthPacketProcessor& proc,
                                const libfreenect2::Freenect2Device::Config& config,
                                uint32_t seed,
                                CollectingFrameListener& listener)
{
  proc.setConfiguration(config);
  loadSyntheticTables(proc, makeIrParams(), seed);
  proc.setFrameListener(&listener);

  std::vector<unsigned char> buffer = makeSyntheticDepthBuffer(seed);
  libfreenect2::DepthPacket packet;
  packet.sequence = seed;
  packet.timestamp = seed * 100u;
  packet.arrival_timestamp_us = seed * 1000u;
  packet.buffer = buffer.data();
  packet.buffer_length = buffer.size();
  packet.memory = 0;
  proc.process(packet);
}

size_t countValidDepthPixels(const libfreenect2::Frame* depth)
{
  if(depth == 0 || depth->format != libfreenect2::Frame::Float)
    return 0;

  const size_t count = depth->width * depth->height;
  const float* values = reinterpret_cast<const float*>(depth->data);
  size_t valid = 0;
  for(size_t i = 0; i < count; ++i)
  {
    if(std::isfinite(values[i]) && values[i] > 0.0f)
      ++valid;
  }
  return valid;
}

DepthFrameAgreement compareDepthFrames(const libfreenect2::Frame* depth_a,
                                       const libfreenect2::Frame* depth_b,
                                       const libfreenect2::Frame* ir_a,
                                       const libfreenect2::Frame* ir_b,
                                       float depth_tolerance_mm)
{
  DepthFrameAgreement agreement = {0.0, 0.0};
  if(depth_a == 0 || depth_b == 0 || ir_a == 0 || ir_b == 0)
    return agreement;

  const size_t count = depth_a->width * depth_a->height;
  if(depth_b->width * depth_b->height != count ||
     ir_a->width * ir_a->height != count || ir_b->width * ir_b->height != count)
    return agreement;

  const float* da = reinterpret_cast<const float*>(depth_a->data);
  const float* db = reinterpret_cast<const float*>(depth_b->data);
  const float* ia = reinterpret_cast<const float*>(ir_a->data);
  const float* ib = reinterpret_cast<const float*>(ir_b->data);
  size_t depth_matches = 0;
  size_t ir_matches = 0;
  for(size_t i = 0; i < count; ++i)
  {
    const bool da_invalid = !std::isfinite(da[i]) || da[i] <= 0.0f;
    const bool db_invalid = !std::isfinite(db[i]) || db[i] <= 0.0f;
    if((da_invalid && db_invalid) ||
       (!da_invalid && !db_invalid && std::fabs(da[i] - db[i]) <= depth_tolerance_mm))
      ++depth_matches;

    const float magnitude = std::fmax(std::fabs(ia[i]), std::fabs(ib[i]));
    const float tolerance = std::fmax(1.0f, 1e-2f * magnitude);
    if(std::fabs(ia[i] - ib[i]) <= tolerance)
      ++ir_matches;
  }

  agreement.depth_ratio = static_cast<double>(depth_matches) / count;
  agreement.ir_ratio = static_cast<double>(ir_matches) / count;
  return agreement;
}

} // namespace testing
} // namespace libfreenect2
