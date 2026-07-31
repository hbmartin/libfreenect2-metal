/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <sstream>

#if defined(_WIN32)
#include <direct.h>
#else
#include <unistd.h>
#endif

#include <libfreenect2/recording.h>
#include <libfreenect2/recording_journal.h>
#include <libfreenect2/recording_manifest.h>
#include <libfreenect2/recording_utils.h>
#include <libfreenect2/protocol/response.h>
#include <libfreenect2/timing.h>

namespace libfreenect2
{
namespace recording
{

namespace
{

std::string uniqueRecordingDirectory()
{
  std::ostringstream path;
  path << "libfreenect2-recording-test-" << monotonicTimeMicroseconds();
  return path.str();
}

void removeTestRecording(const std::string& directory)
{
  std::remove(joinPath(directory, "recording.complete").c_str());
  std::remove(joinPath(directory, "manifest.json").c_str());
  std::remove(joinPath(directory, "calibration/p0.bin").c_str());
  std::remove(joinPath(directory, "frames/color/0000000000.jpg").c_str());
  std::remove(joinPath(directory, "frames/depth/0000000000.depth").c_str());
  std::remove(joinPath(directory, "frames/depth/0000000001.depth").c_str());
  std::remove(joinPath(directory, "frames.ndjson").c_str());
#if defined(_WIN32)
  _rmdir(joinPath(directory, "frames/color").c_str());
  _rmdir(joinPath(directory, "frames/depth").c_str());
  _rmdir(joinPath(directory, "frames").c_str());
  _rmdir(joinPath(directory, "calibration").c_str());
  _rmdir(directory.c_str());
#else
  rmdir(joinPath(directory, "frames/color").c_str());
  rmdir(joinPath(directory, "frames/depth").c_str());
  rmdir(joinPath(directory, "frames").c_str());
  rmdir(joinPath(directory, "calibration").c_str());
  rmdir(directory.c_str());
#endif
}

} // namespace

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

TEST(RecordingWriter, PersistsRawJpegBeforeAppendingItsJournalEntry)
{
  const std::string directory = uniqueRecordingDirectory();
  RecordingWriter writer(directory, 2);
  ASSERT_TRUE(writer.isOpen()) << writer.getLastError();

  Frame frame(1, 1, 4);
  frame.format = Frame::Raw;
  frame.timestamp = 123;
  frame.sequence = 4;
  frame.arrival_timestamp_us = monotonicTimeMicroseconds();
  frame.exposure = 1.25f;
  frame.gain = 2.0f;
  frame.gamma = 1.0f;
  frame.data[0] = 0xff;
  frame.data[1] = 0xd8;
  frame.data[2] = 0xff;
  frame.data[3] = 0xd9;
  EXPECT_FALSE(writer.onNewFrame(Frame::Color, &frame));
  ASSERT_TRUE(writer.close()) << writer.getLastError();

  const RecordingWriter::Stats stats = writer.getStats();
  EXPECT_EQ(1u, stats.written_frames);
  EXPECT_EQ(0u, stats.dropped_frames);
  EXPECT_EQ(4u, stats.written_bytes);

  std::vector<unsigned char> jpeg;
  std::string error;
  ASSERT_TRUE(readFile(joinPath(directory, "frames/color/0000000000.jpg"), jpeg, &error)) << error;
  EXPECT_EQ(4u, jpeg.size());

  std::vector<unsigned char> journal;
  ASSERT_TRUE(readFile(joinPath(directory, "frames.ndjson"), journal, &error)) << error;
  JournalEntry entry;
  ASSERT_TRUE(parseJournalEntry(std::string(journal.begin(), journal.end()), entry, &error))
      << error;
  EXPECT_EQ("color", entry.stream);
  EXPECT_EQ(4u, entry.byte_count);
  EXPECT_EQ(123u, entry.device_timestamp);
  EXPECT_EQ(4u, entry.sequence);
  removeTestRecording(directory);
}

TEST(RecordingWriter, PersistsRawDepthAndP0Calibration)
{
  const std::string directory = uniqueRecordingDirectory();
  RecordingWriter writer(directory, 2);
  ASSERT_TRUE(writer.isOpen()) << writer.getLastError();

  CalibrationData calibration = {};
  calibration.color.fx = 1081.0f;
  calibration.ir.fx = 365.0f;
  calibration.p0_tables.resize(sizeof(protocol::P0TablesResponse), 0x2a);
  ASSERT_TRUE(writer.setCalibration("123456789", "4.0.3912.0", calibration))
      << writer.getLastError();

  Frame frame(1, 1, 4);
  frame.format = Frame::Raw;
  frame.timestamp = 234;
  frame.sequence = 5;
  frame.arrival_timestamp_us = monotonicTimeMicroseconds();
  std::memset(frame.data, 0x55, 4);
  EXPECT_FALSE(writer.onNewFrame(Frame::Depth, &frame));
  ASSERT_TRUE(writer.close()) << writer.getLastError();

  std::vector<unsigned char> depth;
  std::string error;
  ASSERT_TRUE(readFile(joinPath(directory, "frames/depth/0000000000.depth"), depth, &error))
      << error;
  EXPECT_EQ(4u, depth.size());
  EXPECT_EQ(0x55, depth[0]);

  std::vector<unsigned char> p0;
  ASSERT_TRUE(readFile(joinPath(directory, "calibration/p0.bin"), p0, &error)) << error;
  EXPECT_EQ(calibration.p0_tables, p0);
  removeTestRecording(directory);
}

} // namespace recording
} // namespace libfreenect2
