/* Hardware-free tests for the installed vision helpers. */

#include <algorithm>
#include <cmath>
#include <stdint.h>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include <libfreenect2/frame_listener.hpp>
#include <libfreenect2/registration.h>
#include <libfreenect2/vision.h>

#include "support/synthetic.h"

using libfreenect2::Frame;
using libfreenect2::Registration;
using libfreenect2::vision::BGR;
using libfreenect2::vision::DepthSearchOptions;
using libfreenect2::vision::RGB;
using libfreenect2::vision::buildColorToDepthMap;
using libfreenect2::vision::convertColorFrame;
using libfreenect2::vision::findDepthPixel;
using libfreenect2::vision::liftColorPoints;

TEST(Vision, ConvertsBothInputFormatsAndOutputOrders)
{
  const unsigned char bgrx[] = {30, 20, 10, 0, 60, 50, 40, 0};
  Frame bgr(2, 1, 4, const_cast<unsigned char*>(bgrx));
  bgr.format = Frame::BGRX;
  unsigned char output[6] = {};

  ASSERT_TRUE(convertColorFrame(&bgr, RGB, output, sizeof(output)));
  EXPECT_EQ(std::vector<unsigned char>(output, output + 6),
            std::vector<unsigned char>({10, 20, 30, 40, 50, 60}));
  ASSERT_TRUE(convertColorFrame(&bgr, BGR, output, sizeof(output)));
  EXPECT_EQ(std::vector<unsigned char>(output, output + 6),
            std::vector<unsigned char>({30, 20, 10, 60, 50, 40}));

  const unsigned char rgbx[] = {10, 20, 30, 0, 40, 50, 60, 0};
  Frame rgb(2, 1, 4, const_cast<unsigned char*>(rgbx));
  rgb.format = Frame::RGBX;
  ASSERT_TRUE(convertColorFrame(&rgb, BGR, output, sizeof(output)));
  EXPECT_EQ(std::vector<unsigned char>(output, output + 6),
            std::vector<unsigned char>({30, 20, 10, 60, 50, 40}));
  ASSERT_TRUE(convertColorFrame(&rgb, RGB, output, sizeof(output)));
  EXPECT_EQ(std::vector<unsigned char>(output, output + 6),
            std::vector<unsigned char>({10, 20, 30, 40, 50, 60}));
}

TEST(Vision, ValidatesColorConversionBuffersAndOverflow)
{
  Frame frame(1, 1, 4);
  frame.format = Frame::BGRX;
  unsigned char output[3] = {};
  EXPECT_FALSE(convertColorFrame(NULL, BGR, output, sizeof(output)));
  EXPECT_FALSE(convertColorFrame(&frame, BGR, NULL, sizeof(output)));
  EXPECT_FALSE(convertColorFrame(&frame, BGR, output, sizeof(output) - 1));
  frame.format = Frame::Float;
  EXPECT_FALSE(convertColorFrame(&frame, BGR, output, sizeof(output)));

  frame.format = Frame::BGRX;
  frame.width = std::numeric_limits<size_t>::max();
  frame.height = 2;
  EXPECT_FALSE(convertColorFrame(&frame, BGR, output, sizeof(output)));
}

TEST(Vision, ReverseMapKeepsNearestFinitePositiveDepth)
{
  Frame depth(3, 1, 4);
  depth.format = Frame::Float;
  float* values = reinterpret_cast<float*>(depth.data);
  values[0] = 1700.0f;
  values[1] = 900.0f;
  values[2] = std::numeric_limits<float>::quiet_NaN();
  const int mapping[] = {3, 3, 4};
  int32_t reverse[8];
  ASSERT_TRUE(buildColorToDepthMap(&depth, mapping, 3, reverse, 8));
  EXPECT_EQ(reverse[3], 1);
  EXPECT_EQ(reverse[4], -1);
  EXPECT_EQ(reverse[0], -1);
  EXPECT_FALSE(buildColorToDepthMap(&depth, mapping, 2, reverse, 8));
  EXPECT_FALSE(buildColorToDepthMap(&depth, NULL, 3, reverse, 8));
}

TEST(Vision, ExpandsSearchAndSelectsCoherentForeground)
{
  const size_t width = 11;
  const size_t height = 11;
  Frame depth(6, 1, 4);
  depth.format = Frame::Float;
  float* values = reinterpret_cast<float*>(depth.data);
  const float source[] = {1000.0f, 1040.0f, 1080.0f, 2200.0f, 2210.0f, 2220.0f};
  std::copy(source, source + 6, values);
  std::vector<int32_t> reverse(width * height, -1);
  reverse[5 * width + 7] = 0;
  reverse[4 * width + 8] = 1;
  reverse[6 * width + 8] = 2;
  reverse[5 * width + 4] = 3;
  reverse[4 * width + 5] = 4;
  reverse[6 * width + 5] = 5;

  DepthSearchOptions options;
  options.primary_radius = 4;
  options.fallback_radius = 4;
  const int selected = findDepthPixel(0.5f, 0.5f, reverse.data(), reverse.size(), width,
                                      height, &depth, options);
  ASSERT_GE(selected, 0);
  EXPECT_LT(values[selected], 1200.0f);
}

TEST(Vision, UsesFallbackRadiusWhenPrimaryHasNoDepth)
{
  Frame depth(4, 1, 4);
  depth.format = Frame::Float;
  float* values = reinterpret_cast<float*>(depth.data);
  std::fill(values, values + 4, 1000.0f);
  std::vector<int32_t> reverse(11 * 11, -1);
  reverse[5 * 11 + 9] = 2;
  DepthSearchOptions options;
  EXPECT_EQ(options.primary_radius, 8);
  EXPECT_EQ(options.fallback_radius, 20);
  EXPECT_FLOAT_EQ(options.cluster_span_mm, 150.0f);
  options.primary_radius = 2;
  options.fallback_radius = 5;
  EXPECT_EQ(findDepthPixel(0.5f, 0.5f, reverse.data(), reverse.size(), 11, 11, &depth,
                           options),
            2);
}

TEST(Vision, RejectsInvalidSearchInputsAndMissingDepth)
{
  Frame depth(3, 1, 4);
  depth.format = Frame::Float;
  float* values = reinterpret_cast<float*>(depth.data);
  values[0] = 0.0f;
  values[1] = std::numeric_limits<float>::quiet_NaN();
  values[2] = std::numeric_limits<float>::infinity();
  std::vector<int32_t> reverse(25, -1);
  reverse[12] = 0;
  reverse[13] = 1;
  reverse[17] = 2;
  DepthSearchOptions options;
  options.primary_radius = 2;
  options.fallback_radius = 2;
  EXPECT_EQ(findDepthPixel(0.5f, 0.5f, reverse.data(), reverse.size(), 5, 5, &depth,
                           options),
            -1);
  EXPECT_EQ(findDepthPixel(-0.1f, 0.5f, reverse.data(), reverse.size(), 5, 5, &depth,
                           options),
            -1);
  options.fallback_radius = 6;
  EXPECT_EQ(findDepthPixel(0.5f, 0.5f, reverse.data(), reverse.size(), 5, 5, &depth,
                           options),
            -1);
  options.fallback_radius = 2;
  options.cluster_span_mm = std::numeric_limits<float>::quiet_NaN();
  EXPECT_EQ(findDepthPixel(0.5f, 0.5f, reverse.data(), reverse.size(), 5, 5, &depth,
                           options),
            -1);
}

TEST(Vision, LiftsNormalizedPointsToFiniteMetricXyz)
{
  Frame depth(512, 424, 4);
  depth.format = Frame::Float;
  float* values = reinterpret_cast<float*>(depth.data);
  std::fill(values, values + 512 * 424, 0.0f);
  const int depth_index = 212 * 512 + 256;
  values[depth_index] = 1000.0f;
  std::vector<int32_t> reverse(25, -1);
  reverse[12] = depth_index;
  Registration registration(libfreenect2::testing::makeIrParams(),
                            libfreenect2::testing::makeColorParams());
  const float normalized[] = {0.5f, 0.5f, -0.1f, 0.5f};
  float xyz[6] = {};
  uint8_t valid[2] = {};
  int32_t indices[2] = {};
  DepthSearchOptions options;
  options.primary_radius = 1;
  options.fallback_radius = 1;

  ASSERT_TRUE(liftColorPoints(&registration, &depth, reverse.data(), reverse.size(), 5, 5,
                              normalized, 2, options, xyz, valid, indices));
  EXPECT_EQ(valid[0], 1);
  EXPECT_EQ(indices[0], depth_index);
  EXPECT_TRUE(std::isfinite(xyz[0]));
  EXPECT_TRUE(std::isfinite(xyz[1]));
  EXPECT_NEAR(xyz[2], 1.0f, 1e-3f);
  EXPECT_EQ(valid[1], 0);
  EXPECT_EQ(indices[1], -1);
  EXPECT_TRUE(std::isnan(xyz[3]));
  EXPECT_FALSE(liftColorPoints(NULL, &depth, reverse.data(), reverse.size(), 5, 5,
                               normalized, 2, options, xyz, valid, indices));
}
