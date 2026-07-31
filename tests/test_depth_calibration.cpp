/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#include <gtest/gtest.h>

#include <libfreenect2/depth_calibration.h>
#include <libfreenect2/timing.h>

#include <cmath>
#include <cstdio>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace libfreenect2
{
namespace
{

DepthCalibrationSample sample(double known_distance_mm, double measured_median_mm,
                              double mad_mm = 1.0, size_t valid_pixel_count = 100)
{
  DepthCalibrationSample result;
  result.known_distance_mm = known_distance_mm;
  result.measured_median_mm = measured_median_mm;
  result.mad_mm = mad_mm;
  result.valid_pixel_count = valid_pixel_count;
  return result;
}

std::string uniqueProfilePath()
{
  std::ostringstream path;
  path << "libfreenect2-depth-profile-test-" << monotonicTimeMicroseconds() << ".json";
  return path.str();
}

TEST(DepthCalibration, ComputesRobustStatisticsForASyntheticPlane)
{
  Frame depth(7, 5, sizeof(float));
  depth.format = Frame::Float;
  float* pixels = reinterpret_cast<float*>(depth.data);
  for (size_t index = 0; index < depth.width * depth.height; ++index)
    pixels[index] = 1000.0f;

  pixels[1 * depth.width + 1] = 5000.0f;
  pixels[1 * depth.width + 2] = std::numeric_limits<float>::quiet_NaN();
  pixels[1 * depth.width + 3] = 0.0f;
  pixels[2 * depth.width + 2] = 998.0f;
  pixels[2 * depth.width + 3] = 1002.0f;

  DepthRoiStatistics statistics;
  std::string error;
  ASSERT_TRUE(computeDepthRoiStatistics(depth, DepthCalibrationRoi(1, 1, 5, 3), statistics,
                                        &error))
      << error;
  EXPECT_EQ(13u, statistics.valid_pixel_count);
  EXPECT_DOUBLE_EQ(1000.0, statistics.median_mm);
  EXPECT_DOUBLE_EQ(0.0, statistics.mad_mm);
}

TEST(DepthCalibration, AppliesCorrectionWithoutChangingInvalidPixels)
{
  Frame depth(5, 1, sizeof(float));
  depth.format = Frame::Float;
  float* pixels = reinterpret_cast<float*>(depth.data);
  pixels[0] = 1000.0f;
  pixels[1] = 0.0f;
  pixels[2] = -1.0f;
  pixels[3] = std::numeric_limits<float>::quiet_NaN();
  pixels[4] = std::numeric_limits<float>::infinity();

  DepthCorrectionProfile profile;
  profile.scale = 1.1;
  profile.offset_mm = -20.0;
  ASSERT_TRUE(profile.apply(depth));
  EXPECT_FLOAT_EQ(1080.0f, pixels[0]);
  EXPECT_FLOAT_EQ(0.0f, pixels[1]);
  EXPECT_FLOAT_EQ(-1.0f, pixels[2]);
  EXPECT_TRUE(std::isnan(pixels[3]));
  EXPECT_TRUE(std::isinf(pixels[4]));
}

TEST(DepthCalibration, FitsOffsetOnlyFromOneKnownDistance)
{
  std::vector<DepthCalibrationSample> samples;
  samples.push_back(sample(1000.0, 968.0));
  samples.push_back(sample(1000.0, 970.0));
  samples.push_back(sample(1000.0, 972.0));

  DepthCorrectionProfile profile;
  std::string error;
  ASSERT_TRUE(fitDepthCorrectionProfile(samples, profile, &error)) << error;
  EXPECT_EQ(DepthCorrectionProfile::OffsetOnly, profile.model);
  EXPECT_DOUBLE_EQ(1.0, profile.scale);
  EXPECT_DOUBLE_EQ(30.0, profile.offset_mm);
  EXPECT_EQ(samples.size(), profile.residuals_mm.size());
}

TEST(DepthCalibration, FitsLinearModelAndImprovesAnUnseenPlane)
{
  const double expected_scale = 1.02;
  const double expected_offset = -15.0;
  const double known_distances[] = {1000.0, 2000.0, 3000.0};
  std::vector<DepthCalibrationSample> samples;
  for (size_t distance = 0; distance < 3; ++distance)
  {
    const double measured = (known_distances[distance] - expected_offset) / expected_scale;
    samples.push_back(sample(known_distances[distance], measured - 1.0));
    samples.push_back(sample(known_distances[distance], measured));
    samples.push_back(sample(known_distances[distance], measured + 1.0));
  }

  DepthCorrectionProfile profile;
  std::string error;
  ASSERT_TRUE(fitDepthCorrectionProfile(samples, profile, &error)) << error;
  EXPECT_EQ(DepthCorrectionProfile::Linear, profile.model);
  EXPECT_NEAR(expected_scale, profile.scale, 1e-12);
  EXPECT_NEAR(expected_offset, profile.offset_mm, 1e-9);

  const double holdout_known_mm = 2500.0;
  const double holdout_measured_mm = (holdout_known_mm - expected_offset) / expected_scale;
  const double uncorrected_error = std::fabs(holdout_measured_mm - holdout_known_mm);
  const double corrected_error =
      std::fabs(profile.scale * holdout_measured_mm + profile.offset_mm - holdout_known_mm);
  EXPECT_LT(corrected_error, uncorrected_error * 0.25);
}

TEST(DepthCalibration, RejectsAZeroRangeLinearFit)
{
  std::vector<DepthCalibrationSample> samples;
  samples.push_back(sample(1000.0, 900.0));
  samples.push_back(sample(2000.0, 900.0));
  DepthCorrectionProfile profile;
  std::string error;
  EXPECT_FALSE(fitDepthCorrectionProfile(samples, profile, &error));
  EXPECT_FALSE(error.empty());
}

TEST(DepthCalibration, RoundTripsVersionedProfileDiagnostics)
{
  std::vector<DepthCalibrationSample> samples;
  samples.push_back(sample(1000.0, 980.0, 2.5, 120));
  DepthCorrectionProfile profile;
  std::string error;
  ASSERT_TRUE(fitDepthCorrectionProfile(samples, profile, &error)) << error;
  profile.serial = "synthetic-serial";
  profile.firmware = "synthetic-firmware";
  profile.roi = DepthCalibrationRoi(10, 20, 30, 40);

  const std::string path = uniqueProfilePath();
  ASSERT_TRUE(profile.save(path, &error)) << error;
  DepthCorrectionProfile loaded;
  EXPECT_TRUE(DepthCorrectionProfile::load(path, loaded, &error)) << error;
  std::remove(path.c_str());

  EXPECT_EQ(1u, loaded.version);
  EXPECT_EQ(profile.serial, loaded.serial);
  EXPECT_EQ(profile.firmware, loaded.firmware);
  EXPECT_EQ(profile.model, loaded.model);
  EXPECT_DOUBLE_EQ(profile.scale, loaded.scale);
  EXPECT_DOUBLE_EQ(profile.offset_mm, loaded.offset_mm);
  ASSERT_EQ(1u, loaded.samples.size());
  EXPECT_DOUBLE_EQ(2.5, loaded.samples[0].mad_mm);
  EXPECT_EQ(120u, loaded.samples[0].valid_pixel_count);
  ASSERT_EQ(1u, loaded.residuals_mm.size());
  EXPECT_DOUBLE_EQ(profile.residuals_mm[0], loaded.residuals_mm[0]);
}

} // namespace
} // namespace libfreenect2
