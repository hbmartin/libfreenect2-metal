#include <libfreenect2/calibration_profile.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace lf = libfreenect2;

namespace
{

lf::CalibrationProfile sampleProfile()
{
  lf::ProjectiveCameraModel color;
  color.width = 1920;
  color.height = 1080;
  color.fx = 1050.0;
  color.fy = 1048.0;
  color.cx = 959.5;
  color.cy = 539.5;
  color.distortion_model = lf::DistortionModel::BrownConrady5;
  color.distortion = {0.01, -0.02, 0.001, -0.001, 0.003, 0.0, 0.0, 0.0};

  lf::ProjectiveCameraModel ir;
  ir.width = 512;
  ir.height = 424;
  ir.fx = 365.0;
  ir.fy = 366.0;
  ir.cx = 255.5;
  ir.cy = 211.5;
  ir.distortion_model = lf::DistortionModel::Rational8;
  ir.distortion = {0.1, -0.2, 0.001, -0.002, 0.03, 0.01, -0.01, 0.001};

  lf::RigidTransform transform;
  transform.translation_m = {-0.052, 0.001, 0.002};

  lf::DepthCorrectionProfile correction;
  correction.serial = "123456";
  correction.firmware = "4.0.3912.0";
  correction.model = lf::DepthCorrectionProfile::Linear;
  correction.scale = 1.002;
  correction.offset_mm = -12.5;
  correction.rmse_mm = 4.0;

  lf::CalibrationQualityMetrics quality;
  quality.color_views = 25;
  quality.ir_views = 24;
  quality.stereo_views = 22;
  quality.depth_views = 20;
  quality.color_rms_px = 0.4;
  quality.ir_rms_px = 0.3;
  quality.held_out_stereo_rms_px = 0.8;
  quality.depth_rmse_mm = 4.0;

  lf::CalibrationProfile profile;
  profile.setDeviceIdentity("123456", "4.0.3912.0");
  profile.setColorCamera(color);
  profile.setIrCamera(ir);
  profile.setDepthToColor(transform);
  profile.setDepthCorrection(correction);
  profile.setQualityMetrics(quality);
  profile.setProvenance("2026-08-02T00:00:00Z", "0.4.0", "deadbeef");
  return profile;
}

std::filesystem::path temporaryProfilePath()
{
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("libfreenect2-calibration-" + std::to_string(suffix) + ".json");
}

} // namespace

TEST(CalibrationProfile, RoundTripsCanonicalJson)
{
  const std::filesystem::path path = temporaryProfilePath();
  const lf::CalibrationProfile expected = sampleProfile();
  std::string error;
  ASSERT_TRUE(expected.save(path.string(), &error)) << error;

  lf::CalibrationProfile actual;
  ASSERT_TRUE(lf::CalibrationProfile::load(path.string(), actual, &error)) << error;
  EXPECT_EQ(actual.schemaVersion(), 1u);
  EXPECT_EQ(actual.serial(), "123456");
  EXPECT_EQ(actual.firmware(), "4.0.3912.0");
  EXPECT_EQ(actual.irCamera().distortion_model, lf::DistortionModel::Rational8);
  EXPECT_DOUBLE_EQ(actual.depthToColor().translation_m[0], -0.052);
  ASSERT_TRUE(actual.hasDepthCorrection());
  EXPECT_DOUBLE_EQ(actual.depthCorrection().scale, 1.002);
  ASSERT_TRUE(actual.hasQualityMetrics());
  EXPECT_EQ(actual.qualityMetrics().stereo_views, 22u);
  EXPECT_EQ(actual.jobSha256(), "deadbeef");

  std::filesystem::remove(path);
}

TEST(CalibrationProfile, ScalesIntrinsicsAroundPixelCenters)
{
  const lf::ProjectiveCameraModel half = sampleProfile().colorCamera().scaledTo(960, 540);
  EXPECT_DOUBLE_EQ(half.fx, 525.0);
  EXPECT_DOUBLE_EQ(half.cx, 479.5);
  EXPECT_DOUBLE_EQ(half.cy, 269.5);
  EXPECT_TRUE(half.rectified().distortion_model == lf::DistortionModel::None);
}

TEST(CalibrationProfile, RejectsInvalidGeometryAndSerialMismatches)
{
  lf::CalibrationProfile profile = sampleProfile();
  lf::RigidTransform invalid = profile.depthToColor();
  invalid.rotation[0] = 2.0;
  profile.setDepthToColor(invalid);
  std::string error;
  EXPECT_FALSE(profile.isValid(&error));
  EXPECT_NE(error.find("orthonormal"), std::string::npos);

  profile = sampleProfile();
  std::string warning;
  EXPECT_FALSE(profile.matchesDevice("different", "4.0.3912.0", false, &warning, &error));
  EXPECT_TRUE(profile.matchesDevice("different", "new-firmware", true, &warning, &error));
  EXPECT_NE(warning.find("serial mismatch"), std::string::npos);
  EXPECT_NE(warning.find("firmware"), std::string::npos);
}

TEST(CalibrationProfile, CopyAssignsIntoAMovedFromObject)
{
  lf::CalibrationProfile destination = sampleProfile();
  lf::CalibrationProfile moved = std::move(destination);
  EXPECT_EQ(moved.serial(), "123456");

  destination = sampleProfile();
  EXPECT_EQ(destination.serial(), "123456");
  EXPECT_TRUE(destination.isValid());
}

TEST(CalibrationProfile, CopiesFromAMovedFromObjectAsDefaultConstructed)
{
  lf::CalibrationProfile source = sampleProfile();
  const lf::CalibrationProfile moved = std::move(source);
  EXPECT_EQ(moved.serial(), "123456");

  const lf::CalibrationProfile copied(source);
  EXPECT_TRUE(copied.serial().empty());
  EXPECT_FALSE(copied.isValid());

  lf::CalibrationProfile assigned = sampleProfile();
  assigned = source;
  EXPECT_TRUE(assigned.serial().empty());
  EXPECT_FALSE(assigned.isValid());
}

TEST(CalibrationProfile, IgnoresUnknownFieldsAndRejectsOversizedIntegers)
{
  const std::filesystem::path path = temporaryProfilePath();
  std::string error;
  ASSERT_TRUE(sampleProfile().save(path.string(), &error)) << error;

  std::ifstream input(path);
  std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  input.close();
  ASSERT_FALSE(text.empty());
  const size_t root_end = text.find_last_of('}');
  ASSERT_NE(root_end, std::string::npos);
  text.insert(root_end, ",\n  \"future_extension\": {\"accepted\": true}\n");
  {
    std::ofstream output(path, std::ios::trunc);
    output << text;
  }
  lf::CalibrationProfile loaded;
  ASSERT_TRUE(lf::CalibrationProfile::load(path.string(), loaded, &error)) << error;

  const std::string original_width = "\"width\": 1920";
  const size_t width = text.find(original_width);
  ASSERT_NE(width, std::string::npos);
  text.replace(width, original_width.size(), "\"width\": 4294967296");
  {
    std::ofstream output(path, std::ios::trunc);
    output << text;
  }
  EXPECT_FALSE(lf::CalibrationProfile::load(path.string(), loaded, &error));
  EXPECT_NE(error.find("uint32"), std::string::npos);
  std::filesystem::remove(path);
}
