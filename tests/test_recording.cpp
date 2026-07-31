/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#include <direct.h>
#else
#include <unistd.h>
#endif

#include <libfreenect2/recording.h>
#include <libfreenect2/frame_listener_impl.h>
#include <libfreenect2/recording_journal.h>
#include <libfreenect2/recording_loader.h>
#include <libfreenect2/recording_manifest.h>
#include <libfreenect2/recording_utils.h>
#include <libfreenect2/rgb_packet_processor.h>
#include <libfreenect2/timing.h>

#include "support/synthetic.h"

namespace libfreenect2
{
namespace recording
{

namespace
{

std::string uniqueRecordingDirectory()
{
  const char* temporary_root = 0;
#if defined(_WIN32)
  temporary_root = std::getenv("TEMP");
  if (temporary_root == 0 || temporary_root[0] == '\0')
  {
    temporary_root = std::getenv("TMP");
  }
  const std::string root = temporary_root != 0 && temporary_root[0] != '\0' ? temporary_root : ".";
#else
  temporary_root = std::getenv("TMPDIR");
  const std::string root =
      temporary_root != 0 && temporary_root[0] != '\0' ? temporary_root : "/tmp";
#endif
  std::ostringstream path;
  path << "libfreenect2-recording-test-" << monotonicTimeMicroseconds();
  return joinPath(root, path.str());
}

void removeTestRecording(const std::string& directory)
{
  std::vector<unsigned char> journal_bytes;
  std::string ignored_error;
  if (readFile(joinPath(directory, "frames.ndjson"), journal_bytes, &ignored_error))
  {
    std::istringstream journal(std::string(journal_bytes.begin(), journal_bytes.end()));
    std::string line;
    while (std::getline(journal, line))
    {
      JournalEntry entry;
      if (parseJournalEntry(line, entry, 0))
        std::remove(joinPath(directory, entry.path).c_str());
    }
  }
  std::remove(joinPath(directory, "recording.complete").c_str());
  std::remove(joinPath(directory, "manifest.json").c_str());
  std::remove(joinPath(directory, "calibration/p0.bin").c_str());
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

class ScopedRecordingDirectory
{
public:
  ScopedRecordingDirectory() : path_(uniqueRecordingDirectory()) {}
  ~ScopedRecordingDirectory() { removeTestRecording(path_); }

  const std::string& path() const { return path_; }

private:
  ScopedRecordingDirectory(const ScopedRecordingDirectory&);
  ScopedRecordingDirectory& operator=(const ScopedRecordingDirectory&);
  std::string path_;
};

CalibrationData sampleCalibrationData()
{
  CalibrationData calibration = {};
  calibration.color = testing::makeColorParams();
  calibration.ir = testing::makeIrParams();
  calibration.p0_tables = testing::makeSyntheticP0Tables();
  calibration.p0_tables.push_back(0xa5);
  calibration.p0_tables.push_back(0x5a);
  return calibration;
}

class OrderedFrameListener : public FrameListener
{
public:
  virtual bool onNewFrame(Frame::Type type, Frame* frame)
  {
    {
      std::lock_guard<std::mutex> guard(mutex_);
      if (type == Frame::Color || type == Frame::Depth)
      {
        types_.push_back(type);
        delivery_times_.push_back(monotonicTimeMicroseconds());
      }
    }
    delete frame;
    condition_.notify_all();
    return true;
  }

  bool waitForCount(size_t count)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    return condition_.wait_for(lock, std::chrono::seconds(1),
                               [this, count] { return types_.size() >= count; });
  }

  std::vector<Frame::Type> types() const
  {
    std::lock_guard<std::mutex> guard(mutex_);
    return types_;
  }

  std::vector<uint64_t> deliveryTimes() const
  {
    std::lock_guard<std::mutex> guard(mutex_);
    return delivery_times_;
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::vector<Frame::Type> types_;
  std::vector<uint64_t> delivery_times_;
};

class CloseBlockingRgbProcessor : public RgbPacketProcessor
{
public:
  CloseBlockingRgbProcessor()
      : clearing_listener_(false), allow_clear_(false), listener_clear_timed_out_(false),
        processed_(false)
  {
  }

  virtual void setFrameListener(FrameListener* listener)
  {
    if (listener == 0)
    {
      std::unique_lock<std::mutex> lock(mutex_);
      clearing_listener_ = true;
      condition_.notify_all();
      if (!condition_.wait_for(lock, std::chrono::seconds(5), [this] { return allow_clear_; }))
        listener_clear_timed_out_ = true;
    }
    RgbPacketProcessor::setFrameListener(listener);
  }

  virtual void process(const RgbPacket&)
  {
    std::lock_guard<std::mutex> guard(mutex_);
    processed_ = true;
    condition_.notify_all();
  }

  bool waitForListenerClear()
  {
    std::unique_lock<std::mutex> lock(mutex_);
    return condition_.wait_for(lock, std::chrono::seconds(1),
                               [this] { return clearing_listener_; });
  }

  void allowListenerClear()
  {
    {
      std::lock_guard<std::mutex> guard(mutex_);
      allow_clear_ = true;
    }
    condition_.notify_all();
  }

  bool processed() const
  {
    std::lock_guard<std::mutex> guard(mutex_);
    return processed_;
  }

  bool listenerClearTimedOut() const
  {
    std::lock_guard<std::mutex> guard(mutex_);
    return listener_clear_timed_out_;
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  bool clearing_listener_;
  bool allow_clear_;
  bool listener_clear_timed_out_;
  bool processed_;
};

class CloseBlockingPacketPipeline : public DumpPacketPipeline
{
public:
  virtual RgbPacketProcessor* getRgbPacketProcessor() const { return &rgb_processor_; }
  CloseBlockingRgbProcessor* rgbProcessor() { return &rgb_processor_; }

private:
  mutable CloseBlockingRgbProcessor rgb_processor_;
};

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
  EXPECT_FALSE(isSafeRelativePath("..\\manifest.json"));
  EXPECT_FALSE(isSafeRelativePath("frames\\..\\manifest.json"));
  EXPECT_FALSE(isSafeRelativePath("frames\\.\\color.jpg"));
  EXPECT_FALSE(isSafeRelativePath("frames\\\\color.jpg"));
  EXPECT_FALSE(isSafeRelativePath("frames/./color.jpg"));
  EXPECT_FALSE(isSafeRelativePath("frames//color.jpg"));
  EXPECT_FALSE(isSafeRelativePath("frames/color.jpg/"));
  EXPECT_FALSE(isSafeRelativePath("frames\\color.jpg\\"));
  EXPECT_FALSE(isSafeRelativePath("frames/color/data:stream"));
  EXPECT_FALSE(isSafeRelativePath(std::string("frames/color.jpg\0ignored", 24)));
}

namespace
{

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

  ManifestV1 non_finite = sampleManifest();
  non_finite.color.fx = std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(serializeManifestV1(non_finite, text, &error));
  EXPECT_NE(error.find("non-finite"), std::string::npos);
}

TEST(RecordingManifest, RejectsNonIntegerVersions)
{
  ManifestV1 parsed;
  std::string error;
  std::string text;
  ASSERT_TRUE(serializeManifestV1(sampleManifest(), text, &error)) << error;
  const std::string::size_type version = text.find("\"version\": 1");
  ASSERT_NE(version, std::string::npos);

  std::string huge = text;
  huge.replace(version, 12, "\"version\": 1e30");
  EXPECT_FALSE(parseManifestV1(huge, parsed, &error));
  EXPECT_NE(error.find("unsupported"), std::string::npos);

  std::string fractional = text;
  fractional.replace(version, 12, "\"version\": 1.0");
  EXPECT_FALSE(parseManifestV1(fractional, parsed, &error));
  EXPECT_NE(error.find("unsupported"), std::string::npos);
}

TEST(RecordingManifest, RejectsUnsupportedClockSemantics)
{
  ManifestV1 manifest = sampleManifest();
  manifest.device_clock = "unspecified-device-clock";
  std::string text;
  std::string error;
  EXPECT_FALSE(serializeManifestV1(manifest, text, &error));
  EXPECT_NE(error.find("unsupported clock"), std::string::npos);
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

} // namespace

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

  entry = sampleColorEntry();
  entry.path = "frames/color/";
  entry.path.push_back(static_cast<char>(0xff));
  entry.path += ".jpg";
  EXPECT_FALSE(serializeJournalEntry(entry, line, &error));
  EXPECT_NE(error.find("serialize"), std::string::npos);
}

TEST(RecordingJournal, RejectsHostileNumericFields)
{
  JournalEntry entry;
  std::string error;
  EXPECT_FALSE(parseJournalEntry(
      "{\"index\":-1.5,\"stream\":\"depth\",\"path\":\"frames/d.bin\",\"byte_count\":1,"
      "\"device_timestamp\":0,\"sequence\":0,\"arrival_offset_us\":0}\n",
      entry, &error));
  EXPECT_NE(error.find("index"), std::string::npos);
  EXPECT_FALSE(parseJournalEntry(
      "{\"index\":0,\"stream\":\"depth\",\"path\":\"frames/d.bin\",\"byte_count\":-7,"
      "\"device_timestamp\":0,\"sequence\":0,\"arrival_offset_us\":0}\n",
      entry, &error));
  EXPECT_NE(error.find("byte_count"), std::string::npos);
  EXPECT_FALSE(parseJournalEntry(
      "{\"index\":0,\"stream\":\"depth\",\"path\":\"frames/d.bin\",\"byte_count\":1,"
      "\"device_timestamp\":4294967296,\"sequence\":0,\"arrival_offset_us\":0}\n",
      entry, &error));
  EXPECT_NE(error.find("device_timestamp"), std::string::npos);
  EXPECT_FALSE(parseJournalEntry(
      "{\"index\":0,\"stream\":\"color\",\"path\":\"frames/c.jpg\",\"byte_count\":1,"
      "\"device_timestamp\":0,\"sequence\":0,\"arrival_offset_us\":0,"
      "\"exposure\":1e300,\"gain\":1,\"gamma\":1}\n",
      entry, &error));
  EXPECT_NE(error.find("exposure"), std::string::npos);
}

TEST(RecordingWriter, PersistsRawJpegBeforeAppendingItsJournalEntry)
{
  ScopedRecordingDirectory scoped_directory;
  const std::string& directory = scoped_directory.path();
  RecordingWriter writer(directory, 2);
  ASSERT_TRUE(writer.isOpen()) << writer.getLastError();
  ASSERT_TRUE(writer.setCalibration("123456789", "4.0.3912.0", sampleCalibrationData()))
      << writer.getLastError();

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
  EXPECT_EQ(1u, stats.written_color_frames);
  EXPECT_EQ(0u, stats.written_depth_frames);
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

  std::vector<unsigned char> manifest_text;
  ASSERT_TRUE(readFile(joinPath(directory, "manifest.json"), manifest_text, &error)) << error;
  ManifestV1 manifest;
  ASSERT_TRUE(
      parseManifestV1(std::string(manifest_text.begin(), manifest_text.end()), manifest, &error))
      << error;
  EXPECT_EQ("123456789", manifest.serial);
  size_t complete_size = 0;
  EXPECT_TRUE(regularFileSize(joinPath(directory, "recording.complete"), complete_size));
  EXPECT_GT(complete_size, 0u);
}

TEST(RecordingWriter, PersistsRawDepthAndP0Calibration)
{
  ScopedRecordingDirectory scoped_directory;
  const std::string& directory = scoped_directory.path();
  RecordingWriter writer(directory, 2);
  ASSERT_TRUE(writer.isOpen()) << writer.getLastError();

  CalibrationData calibration = sampleCalibrationData();
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
  size_t complete_size = 0;
  EXPECT_TRUE(regularFileSize(joinPath(directory, "recording.complete"), complete_size));
  EXPECT_GT(complete_size, 0u);
  EXPECT_TRUE(writer.close());
}

TEST(RecordingWriter, LeavesAnIncompleteRecordingWithoutCalibration)
{
  ScopedRecordingDirectory scoped_directory;
  const std::string& directory = scoped_directory.path();
  RecordingWriter writer(directory, 1);
  ASSERT_TRUE(writer.isOpen()) << writer.getLastError();
  EXPECT_FALSE(writer.close());
  EXPECT_NE(std::string::npos, writer.getLastError().find("calibration"));
  size_t unused = 0;
  EXPECT_FALSE(regularFileSize(joinPath(directory, "recording.complete"), unused));
}

TEST(RecordingLoader, LoadsIdentityAndCalibrationAndEnforcesCompletion)
{
  ScopedRecordingDirectory scoped_directory;
  const std::string& directory = scoped_directory.path();
  RecordingWriter writer(directory, 1);
  ASSERT_TRUE(writer.isOpen()) << writer.getLastError();
  const CalibrationData expected = sampleCalibrationData();
  ASSERT_TRUE(writer.setCalibration("123456789", "4.0.3912.0", expected)) << writer.getLastError();
  ASSERT_TRUE(writer.close()) << writer.getLastError();

  RecordingMetadata metadata;
  std::string error;
  ASSERT_TRUE(loadRecordingMetadata(directory, false, metadata, &error)) << error;
  EXPECT_EQ("123456789", metadata.manifest.serial);
  EXPECT_EQ("4.0.3912.0", metadata.manifest.firmware);
  EXPECT_EQ(expected.p0_tables, metadata.calibration.p0_tables);

  ASSERT_EQ(0, std::remove(joinPath(directory, "recording.complete").c_str()));
  EXPECT_FALSE(loadRecordingMetadata(directory, false, metadata, &error));
  EXPECT_NE(std::string::npos, error.find("incomplete"));
  EXPECT_TRUE(loadRecordingMetadata(directory, true, metadata, &error)) << error;

  ASSERT_EQ(0, std::remove(joinPath(directory, "calibration/p0.bin").c_str()));
  EXPECT_FALSE(loadRecordingMetadata(directory, true, metadata, &error));
}

TEST(RecordingLoader, SalvagesOnlyCompleteFinalJournalLines)
{
  ScopedRecordingDirectory scoped_directory;
  const std::string& directory = scoped_directory.path();
  RecordingWriter writer(directory, 2);
  ASSERT_TRUE(writer.isOpen()) << writer.getLastError();
  ASSERT_TRUE(writer.setCalibration("123456789", "4.0.3912.0", sampleCalibrationData()))
      << writer.getLastError();
  Frame frame(1, 1, 4);
  frame.format = Frame::Raw;
  frame.arrival_timestamp_us = monotonicTimeMicroseconds();
  std::memset(frame.data, 0x44, 4);
  EXPECT_FALSE(writer.onNewFrame(Frame::Color, &frame));
  ASSERT_TRUE(writer.close()) << writer.getLastError();

  std::vector<unsigned char> bytes;
  std::string error;
  const std::string journal_path = joinPath(directory, "frames.ndjson");
  ASSERT_TRUE(readFile(journal_path, bytes, &error)) << error;
  const std::string valid(bytes.begin(), bytes.end());
  ASSERT_TRUE(writeFileAtomically(journal_path, valid + "{\"truncated\"", &error)) << error;

  RecordingData recording;
  EXPECT_FALSE(loadRecordingData(directory, false, recording, &error));
  ASSERT_TRUE(loadRecordingData(directory, true, recording, &error)) << error;
  ASSERT_EQ(1u, recording.entries.size());

  ASSERT_TRUE(writeFileAtomically(journal_path, valid + "not-json\n{\"truncated\"", &error))
      << error;
  EXPECT_FALSE(loadRecordingData(directory, true, recording, &error));
}

TEST(RecordingReplay, ReplaysJournaledColorWithRecordedMetadata)
{
  ScopedRecordingDirectory scoped_directory;
  const std::string& directory = scoped_directory.path();
  RecordingWriter writer(directory, 2);
  ASSERT_TRUE(writer.isOpen()) << writer.getLastError();
  ASSERT_TRUE(writer.setCalibration("123456789", "4.0.3912.0", sampleCalibrationData()))
      << writer.getLastError();

  Frame source(1, 1, 4);
  source.format = Frame::Raw;
  source.timestamp = 345;
  source.sequence = 6;
  source.arrival_timestamp_us = monotonicTimeMicroseconds();
  source.exposure = 1.25f;
  source.gain = 2.0f;
  source.gamma = 1.0f;
  std::memset(source.data, 0x44, 4);
  EXPECT_FALSE(writer.onNewFrame(Frame::Color, &source));
  ASSERT_TRUE(writer.close()) << writer.getLastError();

  Freenect2Replay replay;
  Freenect2Device* device =
      replay.openRecording(directory, new DumpPacketPipeline(), ReplayOptions());
  ASSERT_NE(static_cast<Freenect2Device*>(0), device);
  EXPECT_EQ("123456789", device->getSerialNumber());
  EXPECT_EQ("4.0.3912.0", device->getFirmwareVersion());
  CalibrationData calibration;
  ASSERT_TRUE(device->getCalibrationData(calibration));
  EXPECT_EQ(sampleCalibrationData().p0_tables, calibration.p0_tables);

  SyncMultiFrameListener listener(Frame::Color);
  device->setColorFrameListener(&listener);
  const uint64_t delivery_start = monotonicTimeMicroseconds();
  ASSERT_TRUE(device->startStreams(true, false));
  FrameMap frames;
  ASSERT_TRUE(listener.waitForNewFrame(frames, 1000));
  ASSERT_NE(frames.end(), frames.find(Frame::Color));
  Frame* actual = frames[Frame::Color];
  EXPECT_EQ(345u, actual->timestamp);
  EXPECT_EQ(6u, actual->sequence);
  EXPECT_GE(actual->arrival_timestamp_us, delivery_start);
  EXPECT_FLOAT_EQ(1.25f, actual->exposure);
  EXPECT_EQ(4u, actual->bytes_per_pixel);
  listener.release(frames);
  EXPECT_TRUE(device->stop());
  EXPECT_TRUE(device->close());
}

TEST(RecordingReplay, HonorsRequestedStreamCombinations)
{
  ScopedRecordingDirectory scoped_directory;
  const std::string& directory = scoped_directory.path();
  RecordingWriter writer(directory, 4);
  ASSERT_TRUE(writer.isOpen()) << writer.getLastError();
  ASSERT_TRUE(writer.setCalibration("123456789", "4.0.3912.0", sampleCalibrationData()))
      << writer.getLastError();

  Frame color(1, 1, 4);
  color.format = Frame::Raw;
  color.timestamp = 100;
  color.sequence = 1;
  color.arrival_timestamp_us = monotonicTimeMicroseconds();
  std::memset(color.data, 0x11, 4);
  EXPECT_FALSE(writer.onNewFrame(Frame::Color, &color));

  const size_t depth_size = 10 * (512 * 424 * 11 / 8);
  Frame depth(1, 1, depth_size);
  depth.format = Frame::Raw;
  depth.timestamp = 200;
  depth.sequence = 2;
  depth.arrival_timestamp_us = monotonicTimeMicroseconds();
  std::memset(depth.data, 0x22, depth_size);
  EXPECT_FALSE(writer.onNewFrame(Frame::Depth, &depth));
  ASSERT_TRUE(writer.close()) << writer.getLastError();

  Freenect2Replay replay;
  Freenect2Device* device =
      replay.openRecording(directory, new DumpPacketPipeline(), ReplayOptions());
  ASSERT_NE(static_cast<Freenect2Device*>(0), device);
  SyncMultiFrameListener color_listener(Frame::Color);
  SyncMultiFrameListener depth_listener(Frame::Depth);
  device->setColorFrameListener(&color_listener);
  device->setIrAndDepthFrameListener(&depth_listener);

  ASSERT_TRUE(device->startStreams(true, false));
  FrameMap frames;
  ASSERT_TRUE(color_listener.waitForNewFrame(frames, 1000));
  EXPECT_EQ(100u, frames[Frame::Color]->timestamp);
  color_listener.release(frames);
  EXPECT_FALSE(depth_listener.waitForNewFrame(frames, 20));
  EXPECT_TRUE(device->stop());

  ASSERT_TRUE(device->startStreams(false, true));
  ASSERT_TRUE(depth_listener.waitForNewFrame(frames, 1000));
  EXPECT_EQ(200u, frames[Frame::Depth]->timestamp);
  depth_listener.release(frames);
  EXPECT_FALSE(color_listener.waitForNewFrame(frames, 20));
  EXPECT_TRUE(device->stop());
  EXPECT_TRUE(device->close());
}

TEST(RecordingReplay, SurvivesConcurrentStopCalls)
{
  ScopedRecordingDirectory scoped_directory;
  const std::string& directory = scoped_directory.path();
  RecordingWriter writer(directory, 2);
  ASSERT_TRUE(writer.isOpen()) << writer.getLastError();
  ASSERT_TRUE(writer.setCalibration("123456789", "4.0.3912.0", sampleCalibrationData()))
      << writer.getLastError();

  Frame source(1, 1, 4);
  source.format = Frame::Raw;
  source.timestamp = 345;
  source.sequence = 6;
  source.arrival_timestamp_us = monotonicTimeMicroseconds();
  std::memset(source.data, 0x44, 4);
  EXPECT_FALSE(writer.onNewFrame(Frame::Color, &source));
  ASSERT_TRUE(writer.close()) << writer.getLastError();

  Freenect2Replay replay;
  Freenect2Device* device =
      replay.openRecording(directory, new DumpPacketPipeline(), ReplayOptions());
  ASSERT_NE(static_cast<Freenect2Device*>(0), device);
  SyncMultiFrameListener listener(Frame::Color);
  device->setColorFrameListener(&listener);

  // Concurrent stop() calls must serialize on the worker-thread handle
  // instead of both joining and deleting it.
  for (int i = 0; i < 25; ++i)
  {
    ASSERT_TRUE(device->startStreams(true, false));
    std::thread first([device]() { device->stop(); });
    std::thread second([device]() { device->stop(); });
    first.join();
    second.join();
    EXPECT_TRUE(device->stop());
  }
  EXPECT_TRUE(device->close());
}

TEST(RecordingReplay, CloseSerializesAConcurrentStart)
{
  ScopedRecordingDirectory scoped_directory;
  const std::string& directory = scoped_directory.path();
  RecordingWriter writer(directory, 2);
  ASSERT_TRUE(writer.isOpen()) << writer.getLastError();
  ASSERT_TRUE(writer.setCalibration("123456789", "4.0.3912.0", sampleCalibrationData()))
      << writer.getLastError();

  Frame source(1, 1, 4);
  source.format = Frame::Raw;
  source.timestamp = 345;
  source.sequence = 6;
  source.arrival_timestamp_us = monotonicTimeMicroseconds();
  std::memset(source.data, 0x44, 4);
  EXPECT_FALSE(writer.onNewFrame(Frame::Color, &source));
  ASSERT_TRUE(writer.close()) << writer.getLastError();

  Freenect2Replay replay;
  CloseBlockingPacketPipeline* pipeline = new CloseBlockingPacketPipeline();
  CloseBlockingRgbProcessor* processor = pipeline->rgbProcessor();
  Freenect2Device* device = replay.openRecording(directory, pipeline, ReplayOptions());
  ASSERT_NE(static_cast<Freenect2Device*>(0), device);

  bool close_result = false;
  std::thread closer([&]() { close_result = device->close(); });
  const bool close_reached_listener_clear = processor->waitForListenerClear();
  if (!close_reached_listener_clear)
  {
    processor->allowListenerClear();
    closer.join();
    ADD_FAILURE() << "close() did not reach listener teardown";
    return;
  }

  std::mutex start_mutex;
  std::condition_variable start_condition;
  bool start_entered = false;
  bool start_complete = false;
  bool start_result = true;
  std::thread starter(
      [&]()
      {
        {
          std::lock_guard<std::mutex> guard(start_mutex);
          start_entered = true;
        }
        start_condition.notify_all();
        const bool result = device->startStreams(true, false);
        {
          std::lock_guard<std::mutex> guard(start_mutex);
          start_result = result;
          start_complete = true;
        }
        start_condition.notify_all();
      });

  bool completed_while_close_was_blocked = false;
  {
    std::unique_lock<std::mutex> lock(start_mutex);
    EXPECT_TRUE(
        start_condition.wait_for(lock, std::chrono::seconds(1), [&]() { return start_entered; }));
    completed_while_close_was_blocked = start_condition.wait_for(
        lock, std::chrono::milliseconds(200), [&]() { return start_complete; });
  }

  processor->allowListenerClear();
  closer.join();
  starter.join();
  EXPECT_TRUE(device->stop());

  EXPECT_FALSE(completed_while_close_was_blocked);
  EXPECT_TRUE(close_result);
  EXPECT_FALSE(start_result);
  EXPECT_FALSE(processor->listenerClearTimedOut());
  EXPECT_FALSE(processor->processed());
  EXPECT_EQ(DeviceClosed, device->getState());
}

TEST(RecordingReplay, PreservesGlobalJournalOrderInFastMode)
{
  ScopedRecordingDirectory scoped_directory;
  const std::string& directory = scoped_directory.path();
  RecordingWriter writer(directory, 4);
  ASSERT_TRUE(writer.isOpen()) << writer.getLastError();
  ASSERT_TRUE(writer.setCalibration("123456789", "4.0.3912.0", sampleCalibrationData()))
      << writer.getLastError();

  Frame color(1, 1, 4);
  color.format = Frame::Raw;
  color.arrival_timestamp_us = monotonicTimeMicroseconds();
  std::memset(color.data, 0x11, 4);
  EXPECT_FALSE(writer.onNewFrame(Frame::Color, &color));
  const size_t depth_size = 10 * (512 * 424 * 11 / 8);
  Frame depth(1, 1, depth_size);
  depth.format = Frame::Raw;
  depth.arrival_timestamp_us = monotonicTimeMicroseconds();
  std::memset(depth.data, 0x22, depth_size);
  EXPECT_FALSE(writer.onNewFrame(Frame::Depth, &depth));
  EXPECT_FALSE(writer.onNewFrame(Frame::Color, &color));
  ASSERT_TRUE(writer.close()) << writer.getLastError();

  Freenect2Replay replay;
  Freenect2Device* device =
      replay.openRecording(directory, new DumpPacketPipeline(), ReplayOptions());
  ASSERT_NE(static_cast<Freenect2Device*>(0), device);
  OrderedFrameListener listener;
  device->setColorFrameListener(&listener);
  device->setIrAndDepthFrameListener(&listener);
  ASSERT_TRUE(device->start());
  ASSERT_TRUE(listener.waitForCount(3));
  const std::vector<Frame::Type> types = listener.types();
  ASSERT_GE(types.size(), 3u);
  EXPECT_EQ(Frame::Color, types[0]);
  EXPECT_EQ(Frame::Depth, types[1]);
  EXPECT_EQ(Frame::Color, types[2]);
  EXPECT_TRUE(device->stop());
  EXPECT_TRUE(device->close());
}

TEST(RecordingReplay, ReproducesRecordedOffsetsAndInterruptsLongWaits)
{
  ScopedRecordingDirectory scoped_directory;
  const std::string& directory = scoped_directory.path();
  RecordingWriter writer(directory, 4);
  ASSERT_TRUE(writer.isOpen()) << writer.getLastError();
  ASSERT_TRUE(writer.setCalibration("123456789", "4.0.3912.0", sampleCalibrationData()))
      << writer.getLastError();

  Frame color(1, 1, 4);
  color.format = Frame::Raw;
  color.arrival_timestamp_us = monotonicTimeMicroseconds();
  std::memset(color.data, 0x11, 4);
  EXPECT_FALSE(writer.onNewFrame(Frame::Color, &color));
  color.arrival_timestamp_us += 100000;
  EXPECT_FALSE(writer.onNewFrame(Frame::Color, &color));
  color.arrival_timestamp_us += 4900000;
  EXPECT_FALSE(writer.onNewFrame(Frame::Color, &color));
  ASSERT_TRUE(writer.close()) << writer.getLastError();

  ReplayOptions options;
  options.reproduce_timing = true;
  Freenect2Replay replay;
  Freenect2Device* device = replay.openRecording(directory, new DumpPacketPipeline(), options);
  ASSERT_NE(static_cast<Freenect2Device*>(0), device);
  OrderedFrameListener listener;
  device->setColorFrameListener(&listener);
  ASSERT_TRUE(device->startStreams(true, false));
  ASSERT_TRUE(listener.waitForCount(2));
  const std::vector<uint64_t> delivery_times = listener.deliveryTimes();
  ASSERT_GE(delivery_times.size(), 2u);
  EXPECT_GE(delivery_times[1] - delivery_times[0], 80000u);
  const uint64_t stop_start = monotonicTimeMicroseconds();
  EXPECT_TRUE(device->stop());
  const uint64_t stop_elapsed = monotonicTimeMicroseconds() - stop_start;
  EXPECT_LT(stop_elapsed, 250000u);
  EXPECT_EQ(2u, listener.types().size());
  EXPECT_TRUE(device->close());
}

TEST(RecordingReplay, RoundTripsSyntheticRawDepthThroughCpuReplay)
{
  ScopedRecordingDirectory scoped_directory;
  const std::string& directory = scoped_directory.path();
  RecordingWriter writer(directory, 2);
  ASSERT_TRUE(writer.isOpen()) << writer.getLastError();
  ASSERT_TRUE(
      writer.setCalibration("synthetic-device", "synthetic-firmware", sampleCalibrationData()))
      << writer.getLastError();

  const std::vector<unsigned char> raw_depth = testing::makeSyntheticDepthBuffer(7);
  Frame source(1, 1, raw_depth.size());
  source.format = Frame::Raw;
  source.timestamp = 700;
  source.sequence = 7;
  source.arrival_timestamp_us = monotonicTimeMicroseconds();
  std::memcpy(source.data, &raw_depth[0], raw_depth.size());
  EXPECT_FALSE(writer.onNewFrame(Frame::Depth, &source));
  ASSERT_TRUE(writer.close()) << writer.getLastError();

  Freenect2Replay replay;
  Freenect2Device* device =
      replay.openRecording(directory, new CpuPacketPipeline(), ReplayOptions());
  ASSERT_NE(static_cast<Freenect2Device*>(0), device);
  SyncMultiFrameListener listener(Frame::Ir | Frame::Depth);
  device->setIrAndDepthFrameListener(&listener);
  ASSERT_TRUE(device->startStreams(false, true));
  FrameMap frames;
  ASSERT_TRUE(listener.waitForNewFrame(frames, 2000));
  ASSERT_NE(frames.end(), frames.find(Frame::Depth));
  EXPECT_EQ(700u, frames[Frame::Depth]->timestamp);
  EXPECT_EQ(7u, frames[Frame::Depth]->sequence);
  EXPECT_EQ(Frame::Float, frames[Frame::Depth]->format);
  EXPECT_EQ(512u, frames[Frame::Depth]->width);
  EXPECT_EQ(424u, frames[Frame::Depth]->height);
  listener.release(frames);
  EXPECT_TRUE(device->stop());
  EXPECT_TRUE(device->close());
}

} // namespace recording
} // namespace libfreenect2
