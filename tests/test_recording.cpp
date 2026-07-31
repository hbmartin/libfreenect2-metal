/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#include <gtest/gtest.h>

#include <libfreenect2/recording_manifest.h>
#include <libfreenect2/recording_journal.h>
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

ManifestV1 sampleManifest()
{
  ManifestV1 manifest;
  manifest.serial = "123456789";
  manifest.firmware = "4.0.3912.0";
  manifest.color.fx = 1081.0f;
  manifest.ir.fx = 365.0f;
  return manifest;
}

TEST(RecordingManifest, RoundTripsVersionOne)
{
  const ManifestV1 expected = sampleManifest();
  std::string text;
  std::string error;
  ASSERT_TRUE(serializeManifestV1(expected, text, &error)) << error;

  ManifestV1 actual;
  ASSERT_TRUE(parseManifestV1(text, actual, &error)) << error;
  EXPECT_EQ(actual.serial, expected.serial);
  EXPECT_EQ(actual.firmware, expected.firmware);
  EXPECT_EQ(actual.color_encoding, "jpeg");
  EXPECT_EQ(actual.depth_encoding, "kinect-v2-raw");
  EXPECT_FLOAT_EQ(actual.color.fx, expected.color.fx);
  EXPECT_FLOAT_EQ(actual.ir.fx, expected.ir.fx);
  EXPECT_EQ(actual.p0_path, "calibration/p0.bin");
}

TEST(RecordingManifest, RejectsUnsupportedVersionsAndUnsafeCalibrationPaths)
{
  std::string text;
  std::string error;
  ASSERT_TRUE(serializeManifestV1(sampleManifest(), text, &error)) << error;

  ManifestV1 parsed;
  std::string unsupported = text;
  const std::string::size_type version = unsupported.find("\"version\": 1");
  ASSERT_NE(version, std::string::npos);
  unsupported.replace(version, 12, "\"version\": 2");
  EXPECT_FALSE(parseManifestV1(unsupported, parsed, &error));
  EXPECT_NE(error.find("unsupported"), std::string::npos);

  std::string unsafe = text;
  const std::string safe_path = "calibration/p0.bin";
  const std::string::size_type path = unsafe.find(safe_path);
  ASSERT_NE(path, std::string::npos);
  unsafe.replace(path, safe_path.size(), "../p0.bin");
  EXPECT_FALSE(parseManifestV1(unsafe, parsed, &error));
  EXPECT_NE(error.find("unsafe"), std::string::npos);
}

TEST(RecordingManifest, RejectsMissingAndNonFiniteCalibration)
{
  ManifestV1 parsed;
  std::string error;
  EXPECT_FALSE(parseManifestV1("{}", parsed, &error));

  std::string text;
  ASSERT_TRUE(serializeManifestV1(sampleManifest(), text, &error)) << error;
  const std::string::size_type fx = text.find("1081.0");
  ASSERT_NE(fx, std::string::npos);
  text.replace(fx, 6, "1e4000");
  EXPECT_FALSE(parseManifestV1(text, parsed, &error));
}

JournalEntry sampleColorEntry()
{
  JournalEntry entry;
  entry.stream = "color";
  entry.path = "frames/color/0000000000.jpg";
  entry.byte_count = 42;
  entry.device_timestamp = 100;
  entry.sequence = 7;
  entry.arrival_offset_us = 1234;
  entry.has_rgb_metadata = true;
  entry.exposure = 1.25f;
  entry.gain = 2.0f;
  entry.gamma = 1.0f;
  return entry;
}

TEST(RecordingJournal, RoundTripsColorMetadata)
{
  const JournalEntry expected = sampleColorEntry();
  std::string line;
  std::string error;
  ASSERT_TRUE(serializeJournalEntry(expected, line, &error)) << error;
  ASSERT_EQ('\n', line[line.size() - 1]);

  JournalEntry actual;
  ASSERT_TRUE(parseJournalEntry(line, actual, &error)) << error;
  EXPECT_EQ(expected.index, actual.index);
  EXPECT_EQ(expected.stream, actual.stream);
  EXPECT_EQ(expected.path, actual.path);
  EXPECT_EQ(expected.byte_count, actual.byte_count);
  EXPECT_EQ(expected.device_timestamp, actual.device_timestamp);
  EXPECT_EQ(expected.sequence, actual.sequence);
  EXPECT_EQ(expected.arrival_offset_us, actual.arrival_offset_us);
  EXPECT_FLOAT_EQ(expected.exposure, actual.exposure);
  EXPECT_FLOAT_EQ(expected.gain, actual.gain);
  EXPECT_FLOAT_EQ(expected.gamma, actual.gamma);
}

TEST(RecordingJournal, RejectsInvalidStreamsPathsAndMetadata)
{
  JournalEntry entry = sampleColorEntry();
  std::string line;
  std::string error;
  entry.stream = "infrared";
  EXPECT_FALSE(serializeJournalEntry(entry, line, &error));
  entry.stream = "color";
  entry.path = "../outside.jpg";
  EXPECT_FALSE(serializeJournalEntry(entry, line, &error));
  entry.path = "frames/color/0000000000.jpg";
  entry.has_rgb_metadata = false;
  EXPECT_FALSE(serializeJournalEntry(entry, line, &error));
  EXPECT_FALSE(parseJournalEntry("{\"index\":0}\n", entry, &error));
}

} // namespace recording
} // namespace libfreenect2
