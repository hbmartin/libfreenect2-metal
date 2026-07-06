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

/** @file test_registration.cpp Pure-math tests for the public Registration API.
 * No hardware, GPU, or I/O — only the exported Registration class + param
 * structs. */

#include <cmath>
#include <cstring>

#include <gtest/gtest.h>

#include <libfreenect2/registration.h>
#include <libfreenect2/frame_listener.hpp>
#include <libfreenect2/libfreenect2.hpp>

#include "support/synthetic.h"

using libfreenect2::Frame;
using libfreenect2::Registration;
using libfreenect2::testing::makeColorParams;
using libfreenect2::testing::makeIrParams;

namespace
{

const int kW = 512;
const int kH = 424;

// Fill a 512x424 float depth frame with a constant depth (mm).
Frame* makeConstantDepthFrame(float depth_mm)
{
  Frame* f = new Frame(kW, kH, 4);
  f->format = Frame::Float;
  float* p = reinterpret_cast<float*>(f->data);
  for (int i = 0; i < kW * kH; ++i)
    p[i] = depth_mm;
  return f;
}

} // namespace

TEST(Registration, ApplyProducesFiniteColorCoordinates)
{
  Registration reg(makeIrParams(), makeColorParams());

  // Sweep a grid of depth pixels at a plausible distance and require the mapped
  // color coordinates to always be finite.
  for (int dy = 0; dy < kH; dy += 53)
  {
    for (int dx = 0; dx < kW; dx += 61)
    {
      float cx = 0.0f, cy = 0.0f;
      reg.apply(dx, dy, 1500.0f /* mm */, cx, cy);
      EXPECT_TRUE(std::isfinite(cx)) << "dx=" << dx << " dy=" << dy;
      EXPECT_TRUE(std::isfinite(cy)) << "dx=" << dx << " dy=" << dy;
    }
  }
}

TEST(Registration, ApplyIsDeterministic)
{
  Registration reg(makeIrParams(), makeColorParams());
  float cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;
  reg.apply(256, 212, 1200.0f, cx0, cy0);
  reg.apply(256, 212, 1200.0f, cx1, cy1);
  EXPECT_FLOAT_EQ(cx0, cx1);
  EXPECT_FLOAT_EQ(cy0, cy1);
}

TEST(Registration, GetPointXYZInvalidDepthIsNaN)
{
  Registration reg(makeIrParams(), makeColorParams());

  // undistorted depth frame: one invalid (0) pixel, one valid pixel.
  Frame* undistorted = makeConstantDepthFrame(0.0f);
  float* p = reinterpret_cast<float*>(undistorted->data);

  float x = 0, y = 0, z = 0;
  reg.getPointXYZ(undistorted, 200, 250, x, y, z);
  EXPECT_TRUE(std::isnan(x));
  EXPECT_TRUE(std::isnan(y));
  EXPECT_TRUE(std::isnan(z));

  // Now make that pixel valid and require a finite, positive-Z point (meters).
  p[200 * kW + 250] = 1000.0f; // 1 m in mm
  reg.getPointXYZ(undistorted, 200, 250, x, y, z);
  EXPECT_TRUE(std::isfinite(x));
  EXPECT_TRUE(std::isfinite(y));
  EXPECT_TRUE(std::isfinite(z));
  EXPECT_GT(z, 0.0f);
  EXPECT_NEAR(z, 1.0f, 1e-3f); // z is returned in meters

  delete undistorted;
}

TEST(Registration, UndistortDepthPreservesShapeAndFiniteness)
{
  Registration reg(makeIrParams(), makeColorParams());

  Frame* depth = makeConstantDepthFrame(1500.0f);
  Frame undistorted(kW, kH, 4);
  undistorted.format = Frame::Float;

  reg.undistortDepth(depth, &undistorted);

  EXPECT_EQ(undistorted.width, static_cast<size_t>(kW));
  EXPECT_EQ(undistorted.height, static_cast<size_t>(kH));

  const float* p = reinterpret_cast<const float*>(undistorted.data);
  for (int i = 0; i < kW * kH; ++i)
    ASSERT_FALSE(std::isinf(p[i])) << "inf at " << i;

  delete depth;
}

TEST(Registration, FullFrameApplyRunsBothFilterModes)
{
  Registration reg(makeIrParams(), makeColorParams());

  Frame rgb(1920, 1080, 4);
  rgb.format = Frame::BGRX;
  std::memset(rgb.data, 0x7f, 1920 * 1080 * 4);

  Frame* depth = makeConstantDepthFrame(1500.0f);
  Frame undistorted(kW, kH, 4);
  Frame registered(kW, kH, 4);

  // Should run without crashing/overrunning under both filter settings
  // (ASan/UBSan guard the memory access in the sanitizer CI job).
  for (int filter = 0; filter <= 1; ++filter)
  {
    reg.apply(&rgb, depth, &undistorted, &registered, filter != 0);
    EXPECT_EQ(registered.width, static_cast<size_t>(kW));
    EXPECT_EQ(registered.height, static_cast<size_t>(kH));
  }

  delete depth;
}
