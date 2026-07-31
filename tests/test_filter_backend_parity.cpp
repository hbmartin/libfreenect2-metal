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

/** @file test_filter_backend_parity.cpp Exercises independent filter controls
 * on accelerator backends using the CPU pipeline as the reference. */

#include <gtest/gtest.h>

#include <libfreenect2/config.h>
#include <libfreenect2/depth_packet_processor.h>

#include "support/synthetic.h"

using libfreenect2::CpuDepthPacketProcessor;
using libfreenect2::testing::CollectingFrameListener;
using libfreenect2::testing::DepthFilterConfiguration;
using libfreenect2::testing::DepthFrameAgreement;
using libfreenect2::testing::compareDepthFrames;
using libfreenect2::testing::countValidDepthPixels;
using libfreenect2::testing::depthFilterConfigurations;
using libfreenect2::testing::runSyntheticDepthProcessor;

#ifdef LIBFREENECT2_WITH_OPENCL_SUPPORT

TEST(OpenCLCpuParity, SupportsEveryFilterCombination)
{
  libfreenect2::OpenCLDepthPacketProcessor opencl;
  if(!opencl.good())
    GTEST_SKIP() << "No usable OpenCL device on this machine";

  const auto& configurations = depthFilterConfigurations();
  for(size_t i = 0; i < configurations.size(); ++i)
  {
    const DepthFilterConfiguration& filter = configurations[i];
    SCOPED_TRACE(filter.name);

    CpuDepthPacketProcessor cpu;
    CollectingFrameListener cpu_output;
    CollectingFrameListener opencl_output;
    runSyntheticDepthProcessor(cpu, filter.config(), 17, cpu_output);
    runSyntheticDepthProcessor(opencl, filter.config(), 17, opencl_output);

    ASSERT_NE(cpu_output.depth(), nullptr);
    ASSERT_NE(opencl_output.depth(), nullptr);
    EXPECT_GT(countValidDepthPixels(opencl_output.depth()), 0u);

    const DepthFrameAgreement agreement = compareDepthFrames(
        cpu_output.depth(), opencl_output.depth(), cpu_output.ir(), opencl_output.ir());
    EXPECT_GT(agreement.ir_ratio, 0.99);
    EXPECT_GT(agreement.depth_ratio, 0.95);
  }
}

#else

TEST(OpenCLCpuParity, SupportsEveryFilterCombination)
{
  GTEST_SKIP() << "Built without OpenCL support";
}

#endif

#ifdef LIBFREENECT2_WITH_OPENGL_SUPPORT

TEST(OpenGLCpuParity, RepeatedConstructionAndTeardownIsStable)
{
  const size_t iterations = 25;
  for (size_t iteration = 0; iteration < iterations; ++iteration)
  {
    libfreenect2::OpenGLDepthPacketProcessor opengl(0, false);
    if (iteration == 0 && !opengl.good())
      GTEST_SKIP() << "No usable OpenGL context on this machine";
    ASSERT_TRUE(opengl.good()) << "iteration " << iteration;
  }
}

TEST(OpenGLCpuParity, SupportsEveryFilterCombination)
{
  libfreenect2::OpenGLDepthPacketProcessor opengl(0, false);
  if (!opengl.good())
    GTEST_SKIP() << "No usable OpenGL context on this machine";

  const auto& configurations = depthFilterConfigurations();
  for(size_t i = 0; i < configurations.size(); ++i)
  {
    const DepthFilterConfiguration& filter = configurations[i];
    SCOPED_TRACE(filter.name);

    CpuDepthPacketProcessor cpu;
    CollectingFrameListener cpu_output;
    CollectingFrameListener opengl_output;
    runSyntheticDepthProcessor(cpu, filter.config(), 19, cpu_output);
    runSyntheticDepthProcessor(opengl, filter.config(), 19, opengl_output);

    ASSERT_NE(cpu_output.depth(), nullptr);
    ASSERT_NE(opengl_output.depth(), nullptr);
    EXPECT_GT(countValidDepthPixels(opengl_output.depth()), 0u);

    const DepthFrameAgreement agreement = compareDepthFrames(
        cpu_output.depth(), opengl_output.depth(), cpu_output.ir(), opengl_output.ir());
    EXPECT_GT(agreement.ir_ratio, 0.99);
    EXPECT_GT(agreement.depth_ratio, 0.95);
  }
}

#else

TEST(OpenGLCpuParity, SupportsEveryFilterCombination)
{
  GTEST_SKIP() << "Built without OpenGL support";
}

#endif
