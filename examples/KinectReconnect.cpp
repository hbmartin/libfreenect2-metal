/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#include <libfreenect2/frame_listener_impl.h>
#include <libfreenect2/libfreenect2.hpp>

#include <iostream>
#include <string>

namespace
{

const unsigned int FRAME_TYPES = libfreenect2::Frame::Color | libfreenect2::Frame::Depth;

bool waitForTerminalState(libfreenect2::Freenect2Device* device)
{
  libfreenect2::SyncMultiFrameListener listener(FRAME_TYPES);
  device->setColorFrameListener(&listener);
  device->setIrAndDepthFrameListener(&listener);
  if (!device->startStreams(true, true))
  {
    device->setColorFrameListener(0);
    device->setIrAndDepthFrameListener(0);
    return false;
  }

  std::cout << "Streaming. Unplug the Kinect to exercise recovery.\n";
  for (;;)
  {
    libfreenect2::FrameMap frames;
    if (listener.waitForNewFrame(frames, 500))
      listener.release(frames);

    const libfreenect2::DeviceState state = device->getState();
    if (state == libfreenect2::DeviceDisconnected || state == libfreenect2::DeviceError)
      break;
    if (state != libfreenect2::DeviceStreaming)
    {
      device->setColorFrameListener(0);
      device->setIrAndDepthFrameListener(0);
      return false;
    }
  }

  device->setColorFrameListener(0);
  device->setIrAndDepthFrameListener(0);
  return true;
}

bool captureAfterRecovery(libfreenect2::Freenect2Device* device)
{
  libfreenect2::SyncMultiFrameListener listener(FRAME_TYPES);
  device->setColorFrameListener(&listener);
  device->setIrAndDepthFrameListener(&listener);
  if (!device->startStreams(true, true))
  {
    device->setColorFrameListener(0);
    device->setIrAndDepthFrameListener(0);
    return false;
  }

  libfreenect2::FrameMap frames;
  const bool captured = listener.waitForNewFrame(frames, 10000);
  if (captured)
    listener.release(frames);
  device->stop();
  device->setColorFrameListener(0);
  device->setIrAndDepthFrameListener(0);
  return captured;
}

} // namespace

int main(int argc, char** argv)
{
  if (argc > 2)
  {
    std::cerr << "usage: KinectReconnect [SERIAL]\n";
    return 2;
  }

  libfreenect2::Freenect2 freenect2;
  if (freenect2.enumerateDevices() == 0)
  {
    std::cerr << "No Kinect v2 detected\n";
    return 1;
  }
  const std::string serial =
      argc == 2 ? std::string(argv[1]) : freenect2.getDefaultDeviceSerialNumber();

  libfreenect2::Freenect2Device* disconnected = freenect2.openDevice(serial);
  if (disconnected == 0)
  {
    std::cerr << "Unable to open Kinect " << serial << "\n";
    return 1;
  }
  if (!waitForTerminalState(disconnected))
  {
    std::cerr << "Streaming stopped without a recoverable terminal state: "
              << disconnected->getLastError() << "\n";
    disconnected->close();
    return 1;
  }

  std::cerr << "Observed terminal state " << disconnected->getState() << ": "
            << disconnected->getLastError() << "\n";
  disconnected->close();

  std::cout << "Reconnect the Kinect; waiting up to 60 seconds for " << serial << "...\n";
  if (!freenect2.waitForDevice(serial, 60000))
  {
    std::cerr << "Kinect did not reappear before the timeout\n";
    return 1;
  }

  libfreenect2::Freenect2Device* recovered = freenect2.openDevice(serial);
  if (recovered == 0 || recovered == disconnected)
  {
    std::cerr << "Recovery did not create a fresh device object\n";
    return 1;
  }
  if (!captureAfterRecovery(recovered))
  {
    std::cerr << "Reopened Kinect did not deliver a frame set: " << recovered->getLastError()
              << "\n";
    recovered->close();
    return 1;
  }

  recovered->close();
  std::cout << "Recovered Kinect " << serial << " with a fresh device object\n";
  return 0;
}
