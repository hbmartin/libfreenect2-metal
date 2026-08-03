#include <libfreenect2/projective_registration.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>

namespace lf = libfreenect2;

namespace
{

lf::ProjectiveCameraModel camera(uint32_t width = 4, uint32_t height = 4)
{
  lf::ProjectiveCameraModel model;
  model.width = width;
  model.height = height;
  model.fx = 2.0;
  model.fy = 2.0;
  model.cx = 1.0;
  model.cy = 1.0;
  return model;
}

lf::CalibrationProfile profileWithCorrection(bool correction)
{
  lf::CalibrationProfile profile;
  profile.setDeviceIdentity("synthetic", "firmware");
  profile.setColorCamera(camera());
  profile.setIrCamera(camera());
  profile.setDepthToColor(lf::RigidTransform());
  if (correction)
  {
    lf::DepthCorrectionProfile depth_correction;
    depth_correction.serial = "synthetic";
    depth_correction.firmware = "firmware";
    depth_correction.scale = 1.0;
    depth_correction.offset_mm = 100.0;
    depth_correction.rmse_mm = 0.0;
    profile.setDepthCorrection(depth_correction);
  }
  return profile;
}

} // namespace

TEST(ProjectiveRegistration, IdentityProjectionSupportsBothRasterizers)
{
  lf::Frame depth(4, 4, sizeof(float), nullptr, lf::Frame::Float);
  std::fill_n(reinterpret_cast<float*>(depth.data), 16, 1000.0f);

  for (lf::RegistrationRasterization mode :
       {lf::RegistrationRasterization::Nearest, lf::RegistrationRasterization::FourNeighborSplat})
  {
    lf::ProjectiveRegistrationOptions options;
    options.rasterization = mode;
    std::string error;
    std::unique_ptr<lf::ProjectiveRegistration> registration =
        lf::ProjectiveRegistration::create(profileWithCorrection(false), camera(), options, &error);
    ASSERT_NE(registration, nullptr) << error;

    lf::Frame output(4, 4, sizeof(float), nullptr, lf::Frame::Float);
    ASSERT_TRUE(registration->apply(depth, output, &error)) << error;
    const float* pixels = reinterpret_cast<const float*>(output.data);
    EXPECT_FLOAT_EQ(pixels[0], 1000.0f);
    EXPECT_FLOAT_EQ(pixels[1], 1000.0f);
    EXPECT_FLOAT_EQ(pixels[4], 1000.0f);
  }
}

TEST(ProjectiveRegistration, AppliesDepthCorrectionOnlyWhenRequested)
{
  lf::Frame depth(4, 4, sizeof(float), nullptr, lf::Frame::Float);
  std::fill_n(reinterpret_cast<float*>(depth.data), 16, 1000.0f);
  std::string error;

  lf::ProjectiveRegistrationOptions raw_options;
  raw_options.rasterization = lf::RegistrationRasterization::Nearest;
  auto raw = lf::ProjectiveRegistration::create(profileWithCorrection(true), camera(), raw_options,
                                                &error);
  ASSERT_NE(raw, nullptr) << error;
  lf::Frame raw_output(4, 4, sizeof(float), nullptr, lf::Frame::Float);
  ASSERT_TRUE(raw->apply(depth, raw_output, &error)) << error;
  EXPECT_FLOAT_EQ(reinterpret_cast<float*>(raw_output.data)[0], 1000.0f);

  lf::ProjectiveRegistrationOptions corrected_options = raw_options;
  corrected_options.apply_depth_correction = true;
  auto corrected = lf::ProjectiveRegistration::create(profileWithCorrection(true), camera(),
                                                      corrected_options, &error);
  ASSERT_NE(corrected, nullptr) << error;
  lf::Frame corrected_output(4, 4, sizeof(float), nullptr, lf::Frame::Float);
  ASSERT_TRUE(corrected->apply(depth, corrected_output, &error)) << error;
  EXPECT_FLOAT_EQ(reinterpret_cast<float*>(corrected_output.data)[0], 1100.0f);
}

TEST(ProjectiveRegistration, ProjectsFromSourcePixelsIntoDifferentTargetIntrinsics)
{
  lf::ProjectiveCameraModel source = camera(4, 4);
  source.fx = source.fy = 1.0;
  source.cx = source.cy = 0.0;
  lf::ProjectiveCameraModel target = camera(8, 8);
  target.fx = target.fy = 2.0;
  target.cx = target.cy = 0.0;
  lf::CalibrationProfile profile = profileWithCorrection(false);
  profile.setIrCamera(source);
  profile.setColorCamera(target);

  lf::ProjectiveRegistrationOptions options;
  options.rasterization = lf::RegistrationRasterization::Nearest;
  std::string error;
  auto registration = lf::ProjectiveRegistration::create(profile, target, options, &error);
  ASSERT_NE(registration, nullptr) << error;

  lf::Frame depth(4, 4, sizeof(float), nullptr, lf::Frame::Float);
  std::fill_n(reinterpret_cast<float*>(depth.data), 16, 0.0f);
  reinterpret_cast<float*>(depth.data)[5] = 1000.0f;
  lf::Frame output(8, 8, sizeof(float), nullptr, lf::Frame::Float);
  ASSERT_TRUE(registration->apply(depth, output, &error)) << error;
  EXPECT_FLOAT_EQ(reinterpret_cast<float*>(output.data)[18], 1000.0f);
  EXPECT_FLOAT_EQ(reinterpret_cast<float*>(output.data)[9], 0.0f);
}

TEST(ProjectiveRegistration, ResolvesCollisionsWithDeterministicNearestDepth)
{
  lf::ProjectiveCameraModel source = camera(4, 4);
  source.fx = source.fy = 100.0;
  source.cx = source.cy = 1.5;
  lf::ProjectiveCameraModel target = camera(2, 2);
  target.fx = target.fy = 0.01;
  target.cx = target.cy = 0.4;
  lf::CalibrationProfile profile = profileWithCorrection(false);
  profile.setIrCamera(source);
  profile.setColorCamera(target);

  lf::ProjectiveRegistrationOptions options;
  options.rasterization = lf::RegistrationRasterization::Nearest;
  std::string error;
  auto registration = lf::ProjectiveRegistration::create(profile, target, options, &error);
  ASSERT_NE(registration, nullptr) << error;

  lf::Frame depth(4, 4, sizeof(float), nullptr, lf::Frame::Float);
  std::fill_n(reinterpret_cast<float*>(depth.data), 16, 0.0f);
  reinterpret_cast<float*>(depth.data)[5] = 1200.0f;
  reinterpret_cast<float*>(depth.data)[6] = 800.0f;
  lf::Frame first(2, 2, sizeof(float), nullptr, lf::Frame::Float);
  lf::Frame second(2, 2, sizeof(float), nullptr, lf::Frame::Float);
  ASSERT_TRUE(registration->apply(depth, first, &error)) << error;
  ASSERT_TRUE(registration->apply(depth, second, &error)) << error;
  EXPECT_FLOAT_EQ(reinterpret_cast<float*>(first.data)[0], 800.0f);
  EXPECT_TRUE(std::equal(reinterpret_cast<float*>(first.data),
                         reinterpret_cast<float*>(first.data) + 4,
                         reinterpret_cast<float*>(second.data)));
}

TEST(ProjectiveRegistration, RejectsUntypedExternalDepth)
{
  float pixels[16] = {};
  lf::Frame depth(4, 4, sizeof(float), reinterpret_cast<unsigned char*>(pixels));
  lf::Frame output(4, 4, sizeof(float), nullptr, lf::Frame::Float);
  std::fill_n(reinterpret_cast<float*>(output.data), 16, 42.0f);
  std::string error;
  auto registration = lf::ProjectiveRegistration::create(
      profileWithCorrection(false), camera(), lf::ProjectiveRegistrationOptions(), &error);
  ASSERT_NE(registration, nullptr) << error;
  EXPECT_FALSE(registration->apply(depth, output, &error));
  EXPECT_FLOAT_EQ(reinterpret_cast<float*>(output.data)[0], 42.0f);
}
