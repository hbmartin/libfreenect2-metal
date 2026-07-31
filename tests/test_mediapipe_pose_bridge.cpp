/* Hardware-free tests for the MediaPipe demo's native mapping helpers. */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <libfreenect2/frame_listener.hpp>
#include <libfreenect2/registration.h>

#include "bridge_helpers.h"
#include "support/synthetic.h"

using mediapipe_pose::buildReverseColorMap;
using mediapipe_pose::convertColorToRgb;
using mediapipe_pose::findDepthPixel;

TEST(MediaPipePoseBridge, ConvertsBgrxAndRgbxToRgb)
{
  const unsigned char bgrx[] = {30, 20, 10, 0, 60, 50, 40, 0};
  const unsigned char rgbx[] = {10, 20, 30, 0, 40, 50, 60, 0};
  unsigned char output[6] = {};

  ASSERT_TRUE(
      convertColorToRgb(bgrx, 2, 1, 4, mediapipe_pose::ColorFormatBgrx, output, sizeof(output)));
  EXPECT_EQ(output[0], 10);
  EXPECT_EQ(output[1], 20);
  EXPECT_EQ(output[2], 30);
  EXPECT_EQ(output[3], 40);
  EXPECT_EQ(output[4], 50);
  EXPECT_EQ(output[5], 60);

  ASSERT_TRUE(
      convertColorToRgb(rgbx, 2, 1, 4, mediapipe_pose::ColorFormatRgbx, output, sizeof(output)));
  EXPECT_EQ(output[0], 10);
  EXPECT_EQ(output[5], 60);
  EXPECT_FALSE(convertColorToRgb(rgbx, 2, 1, 4, mediapipe_pose::ColorFormatRgbx, output,
                                 sizeof(output) - 1));
}

TEST(MediaPipePoseBridge, ReverseMapKeepsNearestDepthOnCollision)
{
  const float depth[] = {1700.0f, 900.0f, 1200.0f};
  const int mapping[] = {3, 3, -1};
  std::vector<int32_t> reverse;
  buildReverseColorMap(depth, 3, mapping, 8, reverse);

  ASSERT_EQ(reverse.size(), 8u);
  EXPECT_EQ(reverse[3], 1);
  EXPECT_EQ(reverse[0], -1);
}

TEST(MediaPipePoseBridge, ExpandsSearchRadiusWhenPrimaryHasNoDepth)
{
  const size_t width = 11, height = 11;
  std::vector<float> depth(4, 1000.0f);
  std::vector<int32_t> reverse(width * height, -1);
  reverse[5 * width + 9] = 2; // Four pixels to the right of the center.

  EXPECT_EQ(
      findDepthPixel(0.5f, 0.5f, reverse, width, height, depth.data(), depth.size(), 2, 5, 150.0f),
      2);
}

TEST(MediaPipePoseBridge, SelectsNearestCoherentForegroundCluster)
{
  const size_t width = 9, height = 9;
  std::vector<float> depth = {1000.0f, 1040.0f, 1080.0f, 2200.0f, 2210.0f, 2220.0f};
  std::vector<int32_t> reverse(width * height, -1);
  reverse[4 * width + 3] = 0;
  reverse[4 * width + 4] = 1;
  reverse[4 * width + 5] = 2;
  reverse[3 * width + 4] = 3;
  reverse[5 * width + 4] = 4;
  reverse[5 * width + 5] = 5;

  const int selected =
      findDepthPixel(0.5f, 0.5f, reverse, width, height, depth.data(), depth.size(), 3, 3, 150.0f);
  ASSERT_GE(selected, 0);
  EXPECT_LT(depth[selected], 1200.0f);
}

TEST(MediaPipePoseBridge, RejectsInvalidOrMissingDepth)
{
  const size_t width = 5, height = 5;
  const float depth[] = {0.0f, NAN, INFINITY};
  std::vector<int32_t> reverse(width * height, -1);
  reverse[2 * width + 2] = 0;
  reverse[2 * width + 3] = 1;
  reverse[3 * width + 2] = 2;

  EXPECT_EQ(findDepthPixel(0.5f, 0.5f, reverse, width, height, depth, 3, 2, 2, 150.0f), -1);
  EXPECT_EQ(findDepthPixel(-0.1f, 0.5f, reverse, width, height, depth, 3, 2, 2, 150.0f), -1);
  EXPECT_EQ(findDepthPixel(0.5f, 0.5f, reverse, width, height, depth, 3, 2, 6, 150.0f), -1);
}

TEST(MediaPipePoseBridge, SelectedDepthProducesFiniteMetricXyz)
{
  const size_t color_width = 5, color_height = 5;
  std::vector<float> search_depth(512 * 424, 0.0f);
  const int depth_index = 212 * 512 + 256;
  search_depth[depth_index] = 1000.0f;
  std::vector<int32_t> reverse(color_width * color_height, -1);
  reverse[2 * color_width + 2] = depth_index;
  ASSERT_EQ(findDepthPixel(0.5f, 0.5f, reverse, color_width, color_height, search_depth.data(),
                           search_depth.size(), 1, 1, 150.0f),
            depth_index);

  libfreenect2::Frame undistorted(512, 424, 4);
  undistorted.format = libfreenect2::Frame::Float;
  float* frame_depth = reinterpret_cast<float*>(undistorted.data);
  std::fill(frame_depth, frame_depth + 512 * 424, 0.0f);
  frame_depth[depth_index] = 1000.0f;
  libfreenect2::Registration registration(libfreenect2::testing::makeIrParams(),
                                          libfreenect2::testing::makeColorParams());
  float x = 0.0f, y = 0.0f, z = 0.0f;
  registration.getPointXYZ(&undistorted, 212, 256, x, y, z);
  EXPECT_TRUE(std::isfinite(x));
  EXPECT_TRUE(std::isfinite(y));
  EXPECT_TRUE(std::isfinite(z));
  EXPECT_NEAR(z, 1.0f, 1e-3f);
}

TEST(MediaPipePoseBridge, ComputesWrapSafeTimestampDelta)
{
  EXPECT_FLOAT_EQ(mediapipe_pose::timestampDeltaMilliseconds(1000, 900), 12.5f);
  EXPECT_FLOAT_EQ(mediapipe_pose::timestampDeltaMilliseconds(2, UINT32_MAX - 1), 0.5f);
}
