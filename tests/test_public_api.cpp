#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <libfreenect2/config.h>
#include <libfreenect2/depth_packet_processor.h>
#include <libfreenect2/libfreenect2.hpp>
#include <libfreenect2/packet_pipeline.h>
#include <libfreenect2/protocol/response.h>
#include <libfreenect2/rgb_packet_processor.h>

namespace libfreenect2
{
bool parseFrameFilename(const std::string& frame_filename, size_t timestamp_sequence[2]);
}

namespace
{

namespace lf = libfreenect2;

void setRgbProcessorEnvironment(const char* value)
{
#if defined(_WIN32)
  _putenv_s("LIBFREENECT2_RGB_PROCESSOR", value == NULL ? "" : value);
#else
  if (value == NULL)
    unsetenv("LIBFREENECT2_RGB_PROCESSOR");
  else
    setenv("LIBFREENECT2_RGB_PROCESSOR", value, 1);
#endif
}

// Internal parser exercised here because replay filenames are part of the
// public Freenect2Replay contract even though the helper is not exported.
namespace libfreenect2_detail
{
bool parse(const std::string& filename, size_t values[2])
{
  return lf::parseFrameFilename(filename, values);
}
} // namespace libfreenect2_detail

#if defined(LIBFREENECT2_WITH_OPENGL_SUPPORT) || defined(LIBFREENECT2_WITH_OPENCL_SUPPORT)
void setPipelineEnvironment(const char* value)
{
#if defined(_WIN32)
  _putenv_s("LIBFREENECT2_PIPELINE", value == NULL ? "" : value);
#else
  if (value == NULL)
    unsetenv("LIBFREENECT2_PIPELINE");
  else
    setenv("LIBFREENECT2_PIPELINE", value, 1);
#endif
}

class ScopedPipelineEnvironment
{
public:
  explicit ScopedPipelineEnvironment(const char* value) : had_previous_(false)
  {
    const char* previous = std::getenv("LIBFREENECT2_PIPELINE");
    if (previous != NULL)
    {
      had_previous_ = true;
      previous_ = previous;
    }
    setPipelineEnvironment(value);
  }

  ~ScopedPipelineEnvironment() { setPipelineEnvironment(had_previous_ ? previous_.c_str() : NULL); }

private:
  bool had_previous_;
  std::string previous_;
};
#endif

bool waitForDeviceState(lf::Freenect2Device* device, lf::DeviceState expected,
                        std::chrono::milliseconds timeout)
{
  const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline)
  {
    if (device->getState() == expected)
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return device->getState() == expected;
}
TEST(PublicApi, ReportsRuntimeIdentity)
{
  EXPECT_EQ(std::string(LIBFREENECT2_VERSION), libfreenect2::getVersion());
  EXPECT_EQ(static_cast<uint32_t>(LIBFREENECT2_API_VERSION), libfreenect2::getApiVersion());
  EXPECT_FALSE(libfreenect2::getBuildRevision().empty());
}

TEST(PublicApi, WaitForDeviceTimesOutForAnUnknownSerial)
{
  libfreenect2::Freenect2 freenect2;
  EXPECT_FALSE(freenect2.waitForDevice("__libfreenect2_missing_serial__", 5, 1));
  EXPECT_FALSE(freenect2.waitForDevice("", 5, 1));
}

TEST(PublicApi, RejectsNegativeDeviceIndex)
{
  libfreenect2::Freenect2 freenect2;
  EXPECT_EQ(static_cast<libfreenect2::Freenect2Device*>(NULL),
            freenect2.openDevice(-1, new libfreenect2::DumpPacketPipeline()));
}

TEST(PublicApi, ExposesCanonicalPipelineFactories)
{
  const std::vector<std::string> compiled = libfreenect2::getCompiledPacketPipelines();
  EXPECT_NE(compiled.end(), std::find(compiled.begin(), compiled.end(), "cpu"));
  EXPECT_NE(compiled.end(), std::find(compiled.begin(), compiled.end(), "dump"));

  for (std::vector<std::string>::const_iterator it = compiled.begin(); it != compiled.end(); ++it)
  {
    libfreenect2::PacketPipeline* pipeline = libfreenect2::createPacketPipeline(*it);
    ASSERT_NE(static_cast<libfreenect2::PacketPipeline*>(NULL), pipeline) << *it;
    EXPECT_EQ(*it, pipeline->getName());
    delete pipeline;
  }

  EXPECT_EQ(static_cast<libfreenect2::PacketPipeline*>(NULL),
            libfreenect2::createPacketPipeline("not-a-pipeline"));
  EXPECT_EQ(static_cast<libfreenect2::PacketPipeline*>(NULL),
            libfreenect2::createPacketPipeline("gl"));
  EXPECT_EQ(static_cast<libfreenect2::PacketPipeline*>(NULL),
            libfreenect2::createPacketPipeline("cl"));
}

#if defined(LIBFREENECT2_WITH_TURBOJPEG_SUPPORT)
TEST(PublicApi, ConfiguresTheRgbDecoderPerPipeline)
{
  lf::PacketPipelineConfig config;
  config.rgb_decoder = lf::PacketPipelineConfig::TurboJPEG;
  config.allow_fallback = false;

  lf::CpuPacketPipeline direct(config);
  ASSERT_NE(static_cast<lf::RgbPacketProcessor*>(NULL), direct.getRgbPacketProcessor());
  EXPECT_STREQ("TurboJPEG", direct.getRgbPacketProcessor()->name());
  EXPECT_TRUE(direct.good());

  lf::PacketPipeline* factory = lf::createPacketPipeline("cpu", config);
  ASSERT_NE(static_cast<lf::PacketPipeline*>(NULL), factory);
  EXPECT_STREQ("TurboJPEG", factory->getRgbPacketProcessor()->name());
  EXPECT_TRUE(factory->good());
  delete factory;
}

TEST(PublicApi, ProgrammaticRgbSelectionOverridesTheEnvironment)
{
  setRgbProcessorEnvironment("vaapi");
  lf::PacketPipelineConfig explicit_config;
  explicit_config.rgb_decoder = lf::PacketPipelineConfig::TurboJPEG;
  explicit_config.allow_fallback = false;
  lf::CpuPacketPipeline explicit_pipeline(explicit_config);
  EXPECT_STREQ("TurboJPEG", explicit_pipeline.getRgbPacketProcessor()->name());

  setRgbProcessorEnvironment("turbojpeg");
  lf::CpuPacketPipeline environment_pipeline;
  EXPECT_STREQ("TurboJPEG", environment_pipeline.getRgbPacketProcessor()->name());
  setRgbProcessorEnvironment(NULL);
}
#endif

#if defined(LIBFREENECT2_WITH_VAAPI_SUPPORT) && defined(LIBFREENECT2_WITH_TURBOJPEG_SUPPORT)
TEST(PublicApi, VaapiInitializationFailureRespectsFallbackPolicy)
{
  lf::PacketPipelineConfig fallback;
  fallback.rgb_decoder = lf::PacketPipelineConfig::VAAPI;
  fallback.vaapi_device = "/dev/libfreenect2-definitely-missing-render-node";
  fallback.allow_fallback = true;
  lf::CpuPacketPipeline recovered(fallback);
  EXPECT_TRUE(recovered.good());
  EXPECT_STREQ("TurboJPEG", recovered.getRgbPacketProcessor()->name());

  lf::PacketPipelineConfig strict = fallback;
  strict.allow_fallback = false;
  lf::CpuPacketPipeline failed(strict);
  EXPECT_FALSE(failed.good());
  EXPECT_STREQ("Unavailable RGB decoder", failed.getRgbPacketProcessor()->name());
}
#endif

TEST(PublicApi, ParsesReplayFilenameFromTheFinalTwoUnderscores)
{
  size_t values[2] = {0, 0};
  EXPECT_TRUE(
      libfreenect2_detail::parse("/tmp/path_with_underscores/color_frame_123_45.jpg", values));
  EXPECT_EQ(123u, values[0]);
  EXPECT_EQ(45u, values[1]);
  EXPECT_FALSE(libfreenect2_detail::parse("color_0_1.jpg", values));
  EXPECT_FALSE(libfreenect2_detail::parse("color_-1_1.jpg", values));
  EXPECT_FALSE(libfreenect2_detail::parse("color_1_1.png", values));
}

#if defined(LIBFREENECT2_WITH_OPENGL_SUPPORT)
TEST(PublicApi, PreservesGlAsEnvironmentAlias)
{
  ScopedPipelineEnvironment environment("gl");
  const std::unique_ptr<lf::PacketPipeline> pipeline(lf::createDefaultPacketPipeline());
  ASSERT_NE(static_cast<lf::PacketPipeline*>(NULL), pipeline.get());
  EXPECT_EQ("opengl", pipeline->getName());
}
#endif

#if defined(LIBFREENECT2_WITH_OPENCL_SUPPORT)
TEST(PublicApi, PreservesClAsEnvironmentAlias)
{
  ScopedPipelineEnvironment environment("cl");
  const std::unique_ptr<lf::PacketPipeline> pipeline(lf::createDefaultPacketPipeline());
  ASSERT_NE(static_cast<lf::PacketPipeline*>(NULL), pipeline.get());
  EXPECT_EQ("opencl", pipeline->getName());
}
#endif

TEST(PublicApi, RejectsInvalidReplayCalibration)
{
  libfreenect2::Freenect2Replay replay;
  libfreenect2::Freenect2Replay::Calibration calibration = {};
  calibration.p0_tables.push_back(0);
  std::vector<std::string> files;
  files.push_back("color_1_1.jpg");

  EXPECT_EQ(static_cast<libfreenect2::Freenect2Device*>(NULL),
            replay.openDevice(files, calibration, new libfreenect2::CpuPacketPipeline()));
}

TEST(PublicApi, ExposesAnImmutableReplayCalibrationSnapshot)
{
  lf::Freenect2Replay replay;
  lf::Freenect2Replay::Calibration expected = {};
  expected.color.fx = 1081.0f;
  expected.ir.fx = 365.0f;
  expected.ir.fy = 365.0f;
  expected.p0_tables.resize(sizeof(lf::protocol::P0TablesResponse));
  expected.x_table.resize(lf::DepthPacketProcessor::TABLE_SIZE);
  expected.z_table.resize(lf::DepthPacketProcessor::TABLE_SIZE);
  expected.lookup_table.resize(lf::DepthPacketProcessor::LUT_SIZE);
  std::vector<std::string> files(1, "missing_color_1_1.jpg");

  lf::Freenect2Device* device = replay.openDevice(files, expected, new lf::CpuPacketPipeline());
  ASSERT_NE(static_cast<lf::Freenect2Device*>(NULL), device);

  lf::CalibrationData actual;
  ASSERT_TRUE(device->getCalibrationData(actual));
  EXPECT_FLOAT_EQ(expected.color.fx, actual.color.fx);
  EXPECT_FLOAT_EQ(expected.ir.fx, actual.ir.fx);
  EXPECT_EQ(expected.p0_tables, actual.p0_tables);
  actual.p0_tables[0] = 42;

  lf::CalibrationData second_copy;
  ASSERT_TRUE(device->getCalibrationData(second_copy));
  EXPECT_EQ(0, second_copy.p0_tables[0]);
  EXPECT_TRUE(device->close());
}

TEST(PublicApi, ReplayStartStopCloseIsRepeatable)
{
  lf::Freenect2Replay replay;
  std::vector<std::string> files(1, "missing_color_1_1.jpg");
  lf::Freenect2Device* device = replay.openDevice(files, new lf::CpuPacketPipeline());
  ASSERT_NE(static_cast<lf::Freenect2Device*>(NULL), device);
  EXPECT_EQ("cpu", device->getPacketPipelineName());
  EXPECT_EQ(lf::DeviceOpen, device->getState());
  EXPECT_TRUE(device->getLastError().empty());
  EXPECT_TRUE(device->startStreams(true, false));
  EXPECT_TRUE(device->stop());
  EXPECT_EQ(lf::DeviceOpen, device->getState());
  EXPECT_TRUE(device->stop());
  EXPECT_TRUE(device->startStreams(true, false));
  EXPECT_TRUE(waitForDeviceState(device, lf::DeviceOpen, std::chrono::seconds(2)));
  EXPECT_TRUE(device->startStreams(true, false));
  EXPECT_TRUE(device->stop());
  const lf::DeviceRuntimeStatistics before_close = device->getRuntimeStatistics();
  EXPECT_EQ(before_close.start_attempts, 3u);
  EXPECT_EQ(before_close.successful_starts, 3u);
  EXPECT_EQ(before_close.stop_calls, 3u);
  EXPECT_TRUE(device->close());
  EXPECT_EQ(lf::DeviceClosed, device->getState());
  EXPECT_FALSE(device->startStreams(true, false));
  EXPECT_FALSE(device->getLastError().empty());
  EXPECT_TRUE(device->close());
}

TEST(PublicApi, LegacyFilenameReplayRetainsSoftwareIdentityAndNeedsNoManifest)
{
  lf::Freenect2Replay replay;
  std::vector<std::string> files(1, "missing_color_1_1.jpg");
  lf::Freenect2Device* device = replay.openDevice(files, new lf::DumpPacketPipeline());
  ASSERT_NE(static_cast<lf::Freenect2Device*>(NULL), device);
  EXPECT_EQ(std::string(LIBFREENECT2_VERSION), device->getSerialNumber());
  EXPECT_EQ(std::string(LIBFREENECT2_VERSION), device->getFirmwareVersion());
  EXPECT_FLOAT_EQ(0.0f, device->getColorCameraParams().fx);
  EXPECT_FLOAT_EQ(0.0f, device->getIrCameraParams().fx);
  lf::CalibrationData calibration;
  EXPECT_FALSE(device->getCalibrationData(calibration));
  EXPECT_TRUE(device->startStreams(true, false));
  EXPECT_TRUE(device->stop());
  EXPECT_TRUE(device->close());
}

TEST(PublicApi, ReplayStateSnapshotsRemainValidDuringLifecycleChanges)
{
  lf::Freenect2Replay replay;
  std::vector<std::string> files(1, "missing_color_1_1.jpg");
  lf::Freenect2Device* device = replay.openDevice(files, new lf::CpuPacketPipeline());
  ASSERT_NE(static_cast<lf::Freenect2Device*>(NULL), device);

  std::atomic<bool> reading(true);
  std::atomic<int> invalid_states(0);
  std::thread reader(
      [&]()
      {
        while (reading.load())
        {
          const lf::DeviceState state = device->getState();
          if (state < lf::DeviceCreated || state > lf::DeviceClosed)
            invalid_states.fetch_add(1);
          (void)device->getLastError();
          (void)device->getRuntimeStatistics();
          std::this_thread::yield();
        }
      });

  for (size_t iteration = 0; iteration < 25; ++iteration)
  {
    EXPECT_TRUE(device->startStreams(true, false));
    EXPECT_TRUE(device->stop());
  }
  reading.store(false);
  reader.join();

  EXPECT_EQ(0, invalid_states.load());
  EXPECT_EQ(lf::DeviceOpen, device->getState());
  EXPECT_TRUE(device->close());
  EXPECT_EQ(lf::DeviceClosed, device->getState());
}

} // namespace
