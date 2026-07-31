/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#include <libfreenect2/depth_calibration.h>
#include <libfreenect2/frame_listener_impl.h>
#include <libfreenect2/libfreenect2.hpp>
#include <libfreenect2/packet_pipeline.h>

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace
{

struct RecordingInput
{
  double known_distance_mm;
  std::string directory;
};

void printUsage()
{
  std::cerr << "usage: KinectDepthCalibration OUTPUT.json --roi X Y WIDTH HEIGHT "
               "[--frames N] [--warmup-frames N] [--serial SERIAL] [--pipeline NAME] "
               "(--recording DISTANCE_MM DIRECTORY | --live DISTANCE_MM) ...\n";
}

bool parsePositiveDouble(const std::string& text, double& value)
{
  errno = 0;
  char* end = 0;
  const double parsed = std::strtod(text.c_str(), &end);
  if (errno != 0 || end == text.c_str() || *end != '\0' || !std::isfinite(parsed) ||
      parsed <= 0.0)
    return false;
  value = parsed;
  return true;
}

bool parseUint32(const std::string& text, uint32_t& value)
{
  if (text.empty() || text[0] == '-')
    return false;
  errno = 0;
  char* end = 0;
  const unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
  if (errno != 0 || end == text.c_str() || *end != '\0' ||
      parsed > std::numeric_limits<uint32_t>::max())
    return false;
  value = static_cast<uint32_t>(parsed);
  return true;
}

bool collectFrames(libfreenect2::Freenect2Device& device, double known_distance_mm,
                   const libfreenect2::DepthCalibrationRoi& roi, uint32_t warmup_frame_count,
                   uint32_t frame_count,
                   std::vector<libfreenect2::DepthCalibrationSample>& samples,
                   std::string& error)
{
  libfreenect2::SyncMultiFrameListener listener(libfreenect2::Frame::Depth);
  device.setIrAndDepthFrameListener(&listener);
  if (!device.startStreams(false, true))
  {
    error = "unable to start replay depth stream";
    return false;
  }

  bool success = true;
  const uint64_t total_frame_count =
      static_cast<uint64_t>(warmup_frame_count) + frame_count;
  for (uint64_t frame_index = 0; frame_index < total_frame_count; ++frame_index)
  {
    libfreenect2::FrameMap frames;
    if (!listener.waitForNewFrame(frames, 10000))
    {
      error = "depth stream ended before the requested calibration frame count";
      success = false;
      break;
    }
    const libfreenect2::FrameMap::const_iterator depth = frames.find(libfreenect2::Frame::Depth);
    libfreenect2::DepthRoiStatistics statistics;
    if (depth == frames.end() ||
        !libfreenect2::computeDepthRoiStatistics(*depth->second, roi, statistics, &error))
    {
      if (depth == frames.end())
        error = "replay delivered a frame set without depth";
      listener.release(frames);
      success = false;
      break;
    }

    if (frame_index >= warmup_frame_count)
    {
      libfreenect2::DepthCalibrationSample sample;
      sample.known_distance_mm = known_distance_mm;
      sample.measured_median_mm = statistics.median_mm;
      sample.mad_mm = statistics.mad_mm;
      sample.valid_pixel_count = statistics.valid_pixel_count;
      samples.push_back(sample);
    }
    listener.release(frames);
  }

  const bool stopped = device.stop();
  if (success && !stopped)
  {
    error = "unable to stop replay depth stream";
    success = false;
  }
  return success;
}

bool collectRecording(const RecordingInput& input,
                      const libfreenect2::DepthCalibrationRoi& roi, uint32_t frame_count,
                      const std::string& pipeline_name,
                      std::string& serial, std::string& firmware,
                      std::vector<libfreenect2::DepthCalibrationSample>& samples,
                      std::string& error)
{
  libfreenect2::PacketPipeline* pipeline = libfreenect2::createPacketPipeline(pipeline_name);
  if (pipeline == 0)
  {
    error = "requested packet pipeline is not compiled in: '" + pipeline_name + "'";
    return false;
  }
  libfreenect2::Freenect2Replay replay;
  libfreenect2::Freenect2Device* device = replay.openRecording(
      input.directory, pipeline, libfreenect2::ReplayOptions());
  if (device == 0)
  {
    error = "unable to open recording directory '" + input.directory + "'";
    return false;
  }

  const std::string recording_serial = device->getSerialNumber();
  const std::string recording_firmware = device->getFirmwareVersion();
  if (serial.empty())
  {
    serial = recording_serial;
    firmware = recording_firmware;
  }
  else if (serial != recording_serial || firmware != recording_firmware)
  {
    std::ostringstream message;
    message << "recording device mismatch: expected " << serial << " / " << firmware
            << ", got " << recording_serial << " / " << recording_firmware;
    error = message.str();
    device->close();
    return false;
  }

  const bool collected = collectFrames(*device, input.known_distance_mm, roi, 0, frame_count,
                                       samples, error);
  const bool closed = device->close();
  if (collected && !closed)
  {
    error = "unable to close replay device";
    return false;
  }
  return collected;
}

bool collectLive(const std::vector<double>& known_distances,
                 const libfreenect2::DepthCalibrationRoi& roi, uint32_t warmup_frame_count,
                 uint32_t frame_count, const std::string& requested_serial,
                 const std::string& pipeline_name, std::string& serial,
                 std::string& firmware,
                 std::vector<libfreenect2::DepthCalibrationSample>& samples,
                 std::string& error)
{
  libfreenect2::Freenect2 freenect2;
  if (freenect2.enumerateDevices() == 0)
  {
    error = "no Kinect v2 device is available for live calibration";
    return false;
  }
  const std::string live_serial =
      requested_serial.empty() ? freenect2.getDefaultDeviceSerialNumber() : requested_serial;
  libfreenect2::PacketPipeline* pipeline = libfreenect2::createPacketPipeline(pipeline_name);
  if (pipeline == 0)
  {
    error = "requested packet pipeline is not compiled in: '" + pipeline_name + "'";
    return false;
  }
  libfreenect2::Freenect2Device* device = freenect2.openDevice(live_serial, pipeline);
  if (device == 0)
  {
    error = "unable to open Kinect '" + live_serial + "' for live calibration";
    return false;
  }

  const std::string live_firmware = device->getFirmwareVersion();
  if (serial.empty())
  {
    serial = live_serial;
    firmware = live_firmware;
  }
  else if (serial != live_serial || firmware != live_firmware)
  {
    std::ostringstream message;
    message << "live device mismatch: expected " << serial << " / " << firmware << ", got "
            << live_serial << " / " << live_firmware;
    error = message.str();
    device->close();
    return false;
  }

  bool success = true;
  for (size_t index = 0; index < known_distances.size(); ++index)
  {
    std::cerr << "Place a flat target at " << known_distances[index]
              << " mm, keep it perpendicular to the depth camera, then press Enter.\n";
    std::string confirmation;
    if (!std::getline(std::cin, confirmation))
    {
      error = "live calibration was cancelled before measurement";
      success = false;
      break;
    }
    if (!collectFrames(*device, known_distances[index], roi, warmup_frame_count, frame_count,
                       samples, error))
    {
      success = false;
      break;
    }
  }

  const bool closed = device->close();
  if (success && !closed)
  {
    error = "unable to close live calibration device";
    return false;
  }
  return success;
}

} // namespace

int main(int argc, char** argv)
{
  if (argc < 8)
  {
    printUsage();
    return 2;
  }

  const std::string output_path = argv[1];
  libfreenect2::DepthCalibrationRoi roi;
  bool have_roi = false;
  uint32_t frame_count = 30;
  uint32_t warmup_frame_count = 30;
  std::string requested_serial;
  std::string pipeline_name = "cpu";
  std::vector<RecordingInput> recordings;
  std::vector<double> live_distances;
  for (int argument = 2; argument < argc; ++argument)
  {
    const std::string option = argv[argument];
    if (option == "--roi" && argument + 4 < argc)
    {
      have_roi = parseUint32(argv[++argument], roi.x) && parseUint32(argv[++argument], roi.y) &&
                 parseUint32(argv[++argument], roi.width) &&
                 parseUint32(argv[++argument], roi.height) && roi.width != 0 && roi.height != 0;
      if (!have_roi)
      {
        printUsage();
        return 2;
      }
    }
    else if (option == "--frames" && argument + 1 < argc)
    {
      if (!parseUint32(argv[++argument], frame_count) || frame_count == 0)
      {
        printUsage();
        return 2;
      }
    }
    else if (option == "--warmup-frames" && argument + 1 < argc)
    {
      if (!parseUint32(argv[++argument], warmup_frame_count))
      {
        printUsage();
        return 2;
      }
    }
    else if (option == "--serial" && argument + 1 < argc)
    {
      requested_serial = argv[++argument];
      if (requested_serial.empty())
      {
        printUsage();
        return 2;
      }
    }
    else if (option == "--pipeline" && argument + 1 < argc)
    {
      pipeline_name = argv[++argument];
      if (pipeline_name.empty())
      {
        printUsage();
        return 2;
      }
    }
    else if (option == "--recording" && argument + 2 < argc)
    {
      RecordingInput input;
      if (!parsePositiveDouble(argv[++argument], input.known_distance_mm))
      {
        printUsage();
        return 2;
      }
      input.directory = argv[++argument];
      recordings.push_back(input);
    }
    else if (option == "--live" && argument + 1 < argc)
    {
      double known_distance_mm = 0.0;
      if (!parsePositiveDouble(argv[++argument], known_distance_mm))
      {
        printUsage();
        return 2;
      }
      live_distances.push_back(known_distance_mm);
    }
    else
    {
      printUsage();
      return 2;
    }
  }

  if (!have_roi || (recordings.empty() && live_distances.empty()))
  {
    printUsage();
    return 2;
  }

  std::string serial;
  std::string firmware;
  std::string error;
  std::vector<libfreenect2::DepthCalibrationSample> samples;
  for (size_t index = 0; index < recordings.size(); ++index)
  {
    if (!collectRecording(recordings[index], roi, frame_count, pipeline_name, serial, firmware,
                          samples, error))
    {
      std::cerr << "Calibration input failed: " << error << "\n";
      return 1;
    }
  }
  if (!live_distances.empty() &&
      !collectLive(live_distances, roi, warmup_frame_count, frame_count, requested_serial,
                   pipeline_name, serial, firmware, samples, error))
  {
    std::cerr << "Live calibration failed: " << error << "\n";
    return 1;
  }

  libfreenect2::DepthCorrectionProfile profile;
  if (!libfreenect2::fitDepthCorrectionProfile(samples, profile, &error))
  {
    std::cerr << "Calibration fit failed: " << error << "\n";
    return 1;
  }
  profile.serial = serial;
  profile.firmware = firmware;
  profile.roi = roi;
  if (!profile.save(output_path, &error))
  {
    std::cerr << "Unable to save calibration profile: " << error << "\n";
    return 1;
  }

  std::cout << "Saved "
            << (profile.model == libfreenect2::DepthCorrectionProfile::OffsetOnly
                    ? "offset-only"
                    : "linear")
            << " profile for " << profile.serial << ": corrected_mm = " << profile.scale
            << " * measured_mm + " << profile.offset_mm << ", RMSE " << profile.rmse_mm
            << " mm\n";
  return 0;
}
