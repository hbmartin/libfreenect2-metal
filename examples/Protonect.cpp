/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * Copyright (c) 2011 individual OpenKinect contributors. See the CONTRIB file
 * for details.
 *
 * This code is licensed to you under the terms of the Apache License, version
 * 2.0, or, at your option, the terms of the GNU General Public License,
 * version 2.0. See the APACHE20 and GPL2 files for the text of the licenses,
 * or the following URLs:
 * http://www.apache.org/licenses/LICENSE-2.0
 * http://www.gnu.org/licenses/gpl-2.0.txt
 *
 * If you redistribute this file in source form, modified or unmodified, you
 * may:
 *   1) Leave this header intact and distribute it under the same terms,
 *      accompanying it with the APACHE20 and GPL20 files, or
 *   2) Delete the Apache 2.0 clause and accompany it with the GPL2 file, or
 *   3) Delete the GPL v2 clause and accompany it with the APACHE20 file
 * In all cases you must keep the copyright notice intact and include a copy
 * of the CONTRIB file.
 *
 * Binary distributions must follow the binary distribution requirements of
 * either License.
 */

/** @file Protonect.cpp Main application file. */

#include <iostream>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <limits>
#include <memory>
#include <signal.h>

/// [headers]
#include <libfreenect2/libfreenect2.hpp>
#include <libfreenect2/frame_listener_impl.h>
#include <libfreenect2/registration.h>
#include <libfreenect2/packet_pipeline.h>
#include <libfreenect2/logger.h>
/// [headers]
#ifdef EXAMPLES_WITH_OPENGL_SUPPORT
#include "viewer.h"
#endif


bool protonect_shutdown = false; ///< Whether the running application should shut down.

void sigint_handler(int)
{
  protonect_shutdown = true;
}

bool protonect_paused = false;
libfreenect2::Freenect2Device *devtopause;

//Doing non-trivial things in signal handler is bad. If you want to pause,
//do it in another thread.
//Though libusb operations are generally thread safe, I cannot guarantee
//everything above is thread safe when calling start()/stop() while
//waitForNewFrame().
void sigusr1_handler(int)
{
  if (devtopause == 0)
    return;
/// [pause]
  if (protonect_paused)
    devtopause->start();
  else
    devtopause->stop();
  protonect_paused = !protonect_paused;
/// [pause]
}

//The following demostrates how to create a custom logger
/// [logger]
#include <fstream>
#include <cstdlib>
class MyFileLogger: public libfreenect2::Logger
{
private:
  std::ofstream logfile_;
public:
  MyFileLogger(const char *filename)
  {
    if (filename)
      logfile_.open(filename);
    level_ = Debug;
  }
  bool good()
  {
    return logfile_.is_open() && logfile_.good();
  }
  virtual void log(Level level, const std::string &message)
  {
    logfile_ << "[" << libfreenect2::Logger::level2str(level) << "] " << message << std::endl;
  }
};
/// [logger]

#if defined(LIBFREENECT2_WITH_OPENGL_SUPPORT) || defined(LIBFREENECT2_WITH_OPENCL_SUPPORT) || \
    defined(LIBFREENECT2_WITH_CUDA_SUPPORT) || defined(LIBFREENECT2_WITH_METAL_SUPPORT)
namespace
{
bool selectExplicitPipeline(std::unique_ptr<libfreenect2::PacketPipeline>& selected,
                            libfreenect2::PacketPipeline* candidate, const char* argument,
                            const char* recovery)
{
  std::unique_ptr<libfreenect2::PacketPipeline> requested(candidate);
  if (requested->good())
  {
    selected.swap(requested);
    return true;
  }

  std::cerr << "Requested pipeline '" << argument << "' is compiled but unavailable at runtime. "
            << recovery << std::endl;
  return false;
}
} // namespace
#endif

/// [main]
/**
 * Main application entry point.
 *
 * Accepted argumemnts:
 * - cpu Perform depth processing with the CPU.
 * - gl  Perform depth processing with OpenGL.
 * - cl  Perform depth processing with OpenCL.
 * - <number> Serial number of the device to open.
 * - -noviewer Disable viewer window.
 */
int main(int argc, char *argv[])
/// [main]
{
  std::string program_path(argv[0]);
  std::cerr << "Version: " << LIBFREENECT2_VERSION << std::endl;
  std::cerr << "Environment variables: LOGFILE=<protonect.log>" << std::endl;
  std::cerr << "Usage: " << program_path << " [-gpu=<id>] [gl | cl | clkde | cuda | cudakde | metal | cpu] [<device serial>]" << std::endl;
  std::cerr << "        [-noviewer] [-norgb | -nodepth] [-help] [-version]" << std::endl;
  std::cerr << "        [-frames <number of frames to process>]" << std::endl;
  std::cerr << "To pause and unpause: pkill -USR1 Protonect" << std::endl;
  size_t executable_name_idx = program_path.rfind("Protonect");

  std::string binpath = "/";

  if(executable_name_idx != std::string::npos)
  {
    binpath = program_path.substr(0, executable_name_idx);
  }

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__)
  // avoid flooing the very slow Windows console with debug messages
  libfreenect2::setGlobalLogger(libfreenect2::createConsoleLogger(libfreenect2::Logger::Info));
#else
  // create a console logger with debug level (default is console logger with info level)
/// [logging]
  libfreenect2::setGlobalLogger(libfreenect2::createConsoleLogger(libfreenect2::Logger::Debug));
/// [logging]
#endif
/// [file logging]
  MyFileLogger *filelogger = new MyFileLogger(getenv("LOGFILE"));
  if (filelogger->good())
    libfreenect2::setGlobalLogger(filelogger);
  else
    delete filelogger;
/// [file logging]

  // Preserve the indentation of source excerpts embedded in the generated docs.
  // clang-format off
/// [context]
  libfreenect2::Freenect2 freenect2;
  libfreenect2::Freenect2Device *dev = 0;
  std::unique_ptr<libfreenect2::PacketPipeline> pipeline;
/// [context]
  // clang-format on

  std::string serial = "";

  bool viewer_enabled = true;
  bool enable_rgb = true;
  bool enable_depth = true;
  int deviceId = -1;
  size_t framemax = -1;

  for(int argI = 1; argI < argc; ++argI)
  {
    const std::string arg(argv[argI]);

    if(arg == "-help" || arg == "--help" || arg == "-h" || arg == "-v" || arg == "--version" || arg == "-version")
    {
      // Just let the initial lines display at the beginning of main
      return 0;
    }
    else if(arg.find("-gpu=") == 0)
    {
      if (pipeline)
      {
        std::cerr << "-gpu must be specified before pipeline argument" << std::endl;
        return -1;
      }
      errno = 0;
      char *end = NULL;
      const long parsed_device_id = std::strtol(argv[argI] + 5, &end, 10);
      if(errno == ERANGE || end == argv[argI] + 5 || *end != '\0' ||
         parsed_device_id < 0 || parsed_device_id > INT_MAX)
      {
        std::cerr << "-gpu requires a non-negative integer device index" << std::endl;
        return -1;
      }
      deviceId = static_cast<int>(parsed_device_id);
    }
    else if(arg == "cpu")
    {
      if(!pipeline)
        // clang-format off
/// [pipeline]
        pipeline.reset(new libfreenect2::CpuPacketPipeline());
/// [pipeline]
      // clang-format on
    }
    else if(arg == "gl")
    {
#ifdef LIBFREENECT2_WITH_OPENGL_SUPPORT
      if (!pipeline &&
          !selectExplicitPipeline(pipeline, new libfreenect2::OpenGLPacketPipeline(), "gl",
                                  "Verify OpenGL 3.1 support and an active display, or select cpu."))
        return -1;
#else
      std::cerr << "Requested pipeline 'gl' is not compiled into Protonect." << std::endl;
      return -1;
#endif
    }
    else if(arg == "cl")
    {
#ifdef LIBFREENECT2_WITH_OPENCL_SUPPORT
      if (!pipeline &&
          !selectExplicitPipeline(pipeline, new libfreenect2::OpenCLPacketPipeline(deviceId), "cl",
                                  "Verify the OpenCL ICD and device with clinfo, or select cpu."))
        return -1;
#else
      std::cerr << "Requested pipeline 'cl' is not compiled into Protonect." << std::endl;
      return -1;
#endif
    }
    else if (arg == "clkde")
    {
#ifdef LIBFREENECT2_WITH_OPENCL_SUPPORT
      if (!pipeline && !selectExplicitPipeline(
                           pipeline, new libfreenect2::OpenCLKdePacketPipeline(deviceId), "clkde",
                           "Verify the OpenCL ICD and device with clinfo, or select cpu."))
        return -1;
#else
      std::cerr << "Requested pipeline 'clkde' is not compiled into Protonect." << std::endl;
      return -1;
#endif
    }
    else if (arg == "cuda")
    {
#ifdef LIBFREENECT2_WITH_CUDA_SUPPORT
      if (!pipeline &&
          !selectExplicitPipeline(
              pipeline, new libfreenect2::CudaPacketPipeline(deviceId), "cuda",
              "Verify the NVIDIA driver, CUDA toolkit, and GPU architecture, or select cpu."))
        return -1;
#else
      std::cerr << "Requested pipeline 'cuda' is not compiled into Protonect." << std::endl;
      return -1;
#endif
    }
    else if (arg == "cudakde")
    {
#ifdef LIBFREENECT2_WITH_CUDA_SUPPORT
      if (!pipeline &&
          !selectExplicitPipeline(
              pipeline, new libfreenect2::CudaKdePacketPipeline(deviceId), "cudakde",
              "Verify the NVIDIA driver, CUDA toolkit, and GPU architecture, or select cpu."))
        return -1;
#else
      std::cerr << "Requested pipeline 'cudakde' is not compiled into Protonect." << std::endl;
      return -1;
#endif
    }
    else if(arg == "metal")
    {
#ifdef LIBFREENECT2_WITH_METAL_SUPPORT
      if (!pipeline &&
          !selectExplicitPipeline(pipeline, new libfreenect2::MetalPacketPipeline(deviceId), "metal",
                                  "Verify a Metal-capable GPU is available, or select cpu."))
        return -1;
#else
      std::cerr << "Requested pipeline 'metal' is not compiled into Protonect." << std::endl;
      return -1;
#endif
    }
    else if(arg.find_first_not_of("0123456789") == std::string::npos) //check if parameter could be a serial number
    {
      serial = arg;
    }
    else if(arg == "-noviewer" || arg == "--noviewer")
    {
      viewer_enabled = false;
    }
    else if(arg == "-norgb" || arg == "--norgb")
    {
      enable_rgb = false;
    }
    else if(arg == "-nodepth" || arg == "--nodepth")
    {
      enable_depth = false;
    }
    else if(arg == "-frames")
    {
      if(argI + 1 >= argc)
      {
        std::cerr << "-frames requires a positive integer" << std::endl;
        return -1;
      }

      const char *frame_count = argv[++argI];
      errno = 0;
      char *end = NULL;
      const unsigned long long parsed_frame_count = std::strtoull(frame_count, &end, 10);
      if (frame_count[0] == '-' || errno == ERANGE || end == frame_count || *end != '\0' ||
          parsed_frame_count == 0 ||
          parsed_frame_count > static_cast<unsigned long long>(std::numeric_limits<size_t>::max()))
      {
        std::cerr << "invalid frame count '" << frame_count << "'" << std::endl;
        return -1;
      }
      framemax = static_cast<size_t>(parsed_frame_count);
    }
    else
    {
      std::cout << "Unknown argument: " << arg << std::endl;
    }
  }

  // Some builds compile without any GPU backend that consumes this option.
  (void)deviceId;

  if (!enable_rgb && !enable_depth)
  {
    std::cerr << "Disabling both streams is not allowed!" << std::endl;
    return -1;
  }

/// [discovery]
  if(freenect2.enumerateDevices() == 0)
  {
    std::cout << "no device connected!" << std::endl;
    return -1;
  }

  if (serial == "")
  {
    serial = freenect2.getDefaultDeviceSerialNumber();
  }
/// [discovery]

  if(pipeline)
  {
    // clang-format off
/// [open]
    dev = freenect2.openDevice(serial, pipeline.release());
/// [open]
    // clang-format on
  }
  else
  {
    dev = freenect2.openDevice(serial);
  }

  if(dev == 0)
  {
    std::cout << "failure opening device!" << std::endl;
    return -1;
  }

  devtopause = dev;

  signal(SIGINT,sigint_handler);
#ifdef SIGUSR1
  signal(SIGUSR1, sigusr1_handler);
#endif
  protonect_shutdown = false;

/// [listeners]
  int types = 0;
  if (enable_rgb)
    types |= libfreenect2::Frame::Color;
  if (enable_depth)
    types |= libfreenect2::Frame::Ir | libfreenect2::Frame::Depth;
  libfreenect2::SyncMultiFrameListener listener(types);
  libfreenect2::FrameMap frames;

  dev->setColorFrameListener(&listener);
  dev->setIrAndDepthFrameListener(&listener);
/// [listeners]

/// [start]
  if (enable_rgb && enable_depth)
  {
    if (!dev->start())
      return -1;
  }
  else
  {
    if (!dev->startStreams(enable_rgb, enable_depth))
      return -1;
  }

  std::cout << "device serial: " << dev->getSerialNumber() << std::endl;
  std::cout << "device firmware: " << dev->getFirmwareVersion() << std::endl;
/// [start]

/// [registration setup]
  libfreenect2::Registration registration(dev->getIrCameraParams(), dev->getColorCameraParams());
  libfreenect2::Frame undistorted(512, 424, 4), registered(512, 424, 4);
/// [registration setup]

  size_t framecount = 0;
#ifdef EXAMPLES_WITH_OPENGL_SUPPORT
  Viewer viewer;
  if (viewer_enabled)
    viewer.initialize();
#else
  if (viewer_enabled)
    std::cout << "Protonect was built without the viewer (GLFW/OpenGL was "
                 "not found at build time); frames will only be counted. "
                 "Install GLFW and rebuild to get the display window."
              << std::endl;
  viewer_enabled = false;
#endif

/// [loop start]
  while(!protonect_shutdown && (framemax == (size_t)-1 || framecount < framemax))
  {
    if (!listener.waitForNewFrame(frames, 10*1000)) // 10 sconds
    {
      std::cout << "timeout!" << std::endl;
      return -1;
    }
    libfreenect2::Frame *rgb = frames[libfreenect2::Frame::Color];
    libfreenect2::Frame *ir = frames[libfreenect2::Frame::Ir];
    libfreenect2::Frame *depth = frames[libfreenect2::Frame::Depth];
    (void)ir;
/// [loop start]

    if (enable_rgb && enable_depth)
    {
/// [registration]
      registration.apply(rgb, depth, &undistorted, &registered);
/// [registration]
    }

    framecount++;
    if (!viewer_enabled)
    {
      if (framecount % 100 == 0)
        std::cout << "The viewer is turned off. Received " << framecount << " frames. Ctrl-C to stop." << std::endl;
      listener.release(frames);
      continue;
    }

#ifdef EXAMPLES_WITH_OPENGL_SUPPORT
    if (enable_rgb)
    {
      viewer.addFrame("RGB", rgb);
    }
    if (enable_depth)
    {
      viewer.addFrame("ir", ir);
      viewer.addFrame("depth", depth);
    }
    if (enable_rgb && enable_depth)
    {
      viewer.addFrame("registered", &registered);
    }

    protonect_shutdown = protonect_shutdown || viewer.render();
#endif

/// [loop end]
    listener.release(frames);
    /** libfreenect2::this_thread::sleep_for(libfreenect2::chrono::milliseconds(100)); */
  }
/// [loop end]

  // TODO: restarting ir stream doesn't work!
  // TODO: bad things will happen, if frame listeners are freed before dev->stop() :(
/// [stop]
  dev->stop();
  dev->close();
/// [stop]

  return 0;
}
