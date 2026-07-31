/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * Copyright (c) 2014 individual OpenKinect contributors. See the CONTRIB file
 * for details.
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#include <libfreenect2/frame_listener_impl.h>
#include <libfreenect2/libfreenect2.hpp>
#include <libfreenect2/packet_pipeline.h>
#include <libfreenect2/registration.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{

void writeColorPpm(const std::string& path, const libfreenect2::Frame& frame)
{
  std::ofstream out(path, std::ios::binary);
  out << "P6\n" << frame.width << " " << frame.height << "\n255\n";
  for (size_t i = 0; i < frame.width * frame.height; ++i)
  {
    const unsigned char* p = frame.data + i * frame.bytes_per_pixel;
    if (frame.format == libfreenect2::Frame::RGBX)
      out.write(reinterpret_cast<const char*>(p), 3);
    else
    {
      const unsigned char rgb[] = {p[2], p[1], p[0]};
      out.write(reinterpret_cast<const char*>(rgb), 3);
    }
  }
}

void hsvToRgb(float hue, unsigned char& r, unsigned char& g, unsigned char& b)
{
  const float c = 1.0f;
  const float h = hue / 60.0f;
  const float x = c * (1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f));
  float rf = 0.0f, gf = 0.0f, bf = 0.0f;
  if (h < 1.0f)
  {
    rf = c;
    gf = x;
  }
  else if (h < 2.0f)
  {
    rf = x;
    gf = c;
  }
  else if (h < 3.0f)
  {
    gf = c;
    bf = x;
  }
  else if (h < 4.0f)
  {
    gf = x;
    bf = c;
  }
  else if (h < 5.0f)
  {
    rf = x;
    bf = c;
  }
  else
  {
    rf = c;
    bf = x;
  }
  r = static_cast<unsigned char>(rf * 255.0f);
  g = static_cast<unsigned char>(gf * 255.0f);
  b = static_cast<unsigned char>(bf * 255.0f);
}

struct DepthStats
{
  size_t valid = 0;
  float minimum = std::numeric_limits<float>::infinity();
  float maximum = 0.0f;
  double sum = 0.0;
};

DepthStats writeDepth(const std::string& raw_path, const std::string& visual_path,
                      const libfreenect2::Frame& frame)
{
  const float* depth = reinterpret_cast<const float*>(frame.data);
  std::ofstream raw(raw_path, std::ios::binary);
  std::ofstream visual(visual_path, std::ios::binary);
  raw << "P5\n" << frame.width << " " << frame.height << "\n65535\n";
  visual << "P6\n" << frame.width << " " << frame.height << "\n255\n";
  DepthStats stats;

  for (size_t i = 0; i < frame.width * frame.height; ++i)
  {
    const float d = depth[i];
    const bool valid = std::isfinite(d) && d > 0.0f;
    const uint16_t millimeters = valid ? static_cast<uint16_t>(std::min(d, 65535.0f)) : 0;
    const unsigned char be[] = {static_cast<unsigned char>(millimeters >> 8),
                                static_cast<unsigned char>(millimeters & 0xff)};
    raw.write(reinterpret_cast<const char*>(be), 2);

    unsigned char rgb[] = {0, 0, 0};
    if (valid)
    {
      stats.valid++;
      stats.minimum = std::min(stats.minimum, d);
      stats.maximum = std::max(stats.maximum, d);
      stats.sum += d;
      const float normalized = std::max(0.0f, std::min(1.0f, (d - 500.0f) / 4000.0f));
      hsvToRgb(normalized * 240.0f, rgb[0], rgb[1], rgb[2]);
    }
    visual.write(reinterpret_cast<const char*>(rgb), 3);
  }
  return stats;
}

void writeIrPgm(const std::string& path, const libfreenect2::Frame& frame)
{
  const float* ir = reinterpret_cast<const float*>(frame.data);
  std::vector<float> finite;
  finite.reserve(frame.width * frame.height);
  for (size_t i = 0; i < frame.width * frame.height; ++i)
    if (std::isfinite(ir[i]) && ir[i] > 0.0f)
      finite.push_back(ir[i]);

  float low = 0.0f, high = 1.0f;
  if (!finite.empty())
  {
    const size_t low_index = finite.size() / 100;
    const size_t high_index = finite.size() * 99 / 100;
    std::nth_element(finite.begin(), finite.begin() + low_index, finite.end());
    low = finite[low_index];
    std::nth_element(finite.begin(), finite.begin() + high_index, finite.end());
    high = std::max(low + 1.0f, finite[high_index]);
  }

  std::ofstream out(path, std::ios::binary);
  out << "P5\n" << frame.width << " " << frame.height << "\n255\n";
  for (size_t i = 0; i < frame.width * frame.height; ++i)
  {
    float normalized = 0.0f;
    if (std::isfinite(ir[i]) && ir[i] > 0.0f)
    {
      normalized = (ir[i] - low) / (high - low);
      normalized = std::max(0.0f, std::min(1.0f, normalized));
    }
    const unsigned char value = static_cast<unsigned char>(std::sqrt(normalized) * 255.0f);
    out.write(reinterpret_cast<const char*>(&value), 1);
  }
}

} // namespace

int main(int argc, char** argv)
{
  if (argc != 2)
  {
    std::cerr << "usage: KinectCapture OUTPUT_DIRECTORY\n";
    return 2;
  }
  const std::string output = argv[1];

  std::ofstream output_probe(output + "/metadata.json", std::ios::app);
  if (!output_probe)
  {
    std::cerr << "Output directory does not exist or is not writable: " << output << "\n";
    return 2;
  }
  output_probe.close();

  libfreenect2::Freenect2 freenect2;
  if (freenect2.enumerateDevices() == 0)
  {
    std::cerr << "No Kinect v2 detected\n";
    return 1;
  }
  const std::string serial = freenect2.getDefaultDeviceSerialNumber();

#ifdef LIBFREENECT2_WITH_METAL_SUPPORT
  libfreenect2::PacketPipeline* pipeline = new libfreenect2::MetalPacketPipeline();
  const char* pipeline_name = "Metal";
#else
  libfreenect2::PacketPipeline* pipeline = new libfreenect2::CpuPacketPipeline();
  const char* pipeline_name = "CPU";
#endif

  // openDevice transfers pipeline ownership to its internal device object.
  libfreenect2::Freenect2Device* device = freenect2.openDevice(serial, pipeline);
  if (!device)
  {
    std::cerr << "Unable to open Kinect " << serial << "\n";
    return 1;
  }

  libfreenect2::SyncMultiFrameListener listener(
      libfreenect2::Frame::Color | libfreenect2::Frame::Ir | libfreenect2::Frame::Depth);
  device->setColorFrameListener(&listener);
  device->setIrAndDepthFrameListener(&listener);
  if (!device->start())
  {
    std::cerr << "Unable to start Kinect streams\n";
    device->close();
    return 1;
  }

  libfreenect2::FrameMap frames;
  for (int i = 0; i < 20; ++i)
  {
    if (!listener.waitForNewFrame(frames, 10000))
    {
      std::cerr << "Timed out waiting for synchronized frame " << i << "\n";
      device->stop();
      device->close();
      return 1;
    }
    if (i != 19)
      listener.release(frames);
  }

  libfreenect2::Frame* color = frames[libfreenect2::Frame::Color];
  libfreenect2::Frame* ir = frames[libfreenect2::Frame::Ir];
  libfreenect2::Frame* depth = frames[libfreenect2::Frame::Depth];
  libfreenect2::Registration registration(device->getIrCameraParams(),
                                          device->getColorCameraParams());
  libfreenect2::Frame undistorted(512, 424, 4);
  libfreenect2::Frame registered(512, 424, 4);
  registered.format = libfreenect2::Frame::BGRX;
  registration.apply(color, depth, &undistorted, &registered);

  writeColorPpm(output + "/rgb.ppm", *color);
  writeColorPpm(output + "/registered.ppm", registered);
  writeIrPgm(output + "/infrared.pgm", *ir);
  const DepthStats stats =
      writeDepth(output + "/depth_mm.pgm", output + "/depth_false_color.ppm", *depth);

  std::ofstream metadata(output + "/metadata.json");
  metadata << std::fixed << std::setprecision(2) << "{\n"
           << "  \"serial\": \"" << serial << "\",\n"
           << "  \"firmware\": \"" << device->getFirmwareVersion() << "\",\n"
           << "  \"pipeline\": \"" << pipeline_name << "\",\n"
           << "  \"color_size\": [" << color->width << ", " << color->height << "],\n"
           << "  \"depth_size\": [" << depth->width << ", " << depth->height << "],\n"
           << "  \"valid_depth_pixels\": " << stats.valid << ",\n"
           << "  \"valid_depth_percent\": "
           << (100.0 * stats.valid / (depth->width * depth->height)) << ",\n"
           << "  \"minimum_depth_mm\": " << (stats.valid ? stats.minimum : 0.0f) << ",\n"
           << "  \"mean_depth_mm\": " << (stats.valid ? stats.sum / stats.valid : 0.0) << ",\n"
           << "  \"maximum_depth_mm\": " << (stats.valid ? stats.maximum : 0.0f) << "\n"
           << "}\n";

  listener.release(frames);
  device->stop();
  device->close();
  std::cout << "Captured Kinect " << serial << ": " << stats.valid << " valid depth pixels\n";
  return 0;
}
