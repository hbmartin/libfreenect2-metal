/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#include <gtest/gtest.h>

#include <libfreenect2/recording_utils.h>

namespace libfreenect2
{
namespace recording
{

TEST(RecordingPaths, AcceptsCanonicalRecordingPaths)
{
  EXPECT_TRUE(isSafeRelativePath("manifest.json"));
  EXPECT_TRUE(isSafeRelativePath("calibration/p0.bin"));
  EXPECT_TRUE(isSafeRelativePath("frames/color/0000000000.jpg"));
  EXPECT_TRUE(isSafeRelativePath("frames\\depth\\0000000001.depth"));
}

TEST(RecordingPaths, RejectsAbsoluteAndTraversalPaths)
{
  EXPECT_FALSE(isSafeRelativePath(""));
  EXPECT_FALSE(isSafeRelativePath("/absolute/path"));
  EXPECT_FALSE(isSafeRelativePath("\\absolute\\path"));
  EXPECT_FALSE(isSafeRelativePath("C:\\absolute\\path"));
  EXPECT_FALSE(isSafeRelativePath("../manifest.json"));
  EXPECT_FALSE(isSafeRelativePath("frames/../manifest.json"));
  EXPECT_FALSE(isSafeRelativePath("frames/./color.jpg"));
  EXPECT_FALSE(isSafeRelativePath("frames//color.jpg"));
  EXPECT_FALSE(isSafeRelativePath("frames/color.jpg/"));
  EXPECT_FALSE(isSafeRelativePath(std::string("frames/color.jpg\0ignored", 24)));
}

} // namespace recording
} // namespace libfreenect2
