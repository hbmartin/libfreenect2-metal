/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * This code is licensed under either the Apache License, Version 2.0, or the
 * GNU General Public License, Version 2.0. See APACHE20 and GPL2.
 */

#ifndef LIBFREENECT2_USB_ERROR_H_
#define LIBFREENECT2_USB_ERROR_H_

#include <libusb.h>

#include <sstream>
#include <string>

namespace libfreenect2
{
namespace usb
{

inline std::string formatLibusbError(int result, bool include_debug_hint = false)
{
  std::ostringstream message;
  message << libusb_error_name(result) << " " << libusb_strerror(static_cast<libusb_error>(result));
  if (include_debug_hint)
    message << ". Set LIBUSB_DEBUG=3 in the process environment for more debug output.";
  return message.str();
}

inline std::string formatLibusbSpeed(int speed)
{
  switch (speed)
  {
  case LIBUSB_SPEED_LOW:
    return "Low-Speed (1.5 Mb/s)";
  case LIBUSB_SPEED_FULL:
    return "Full-Speed (12 Mb/s)";
  case LIBUSB_SPEED_HIGH:
    return "High-Speed (480 Mb/s)";
  case LIBUSB_SPEED_SUPER:
    return "SuperSpeed (5 Gb/s)";
  case LIBUSB_SPEED_UNKNOWN:
    return "unknown";
  default:
    if (speed > LIBUSB_SPEED_SUPER)
      return "SuperSpeed or faster";
    std::ostringstream message;
    message << "unknown (libusb value " << speed << ")";
    return message.str();
  }
}

inline bool isKnownSubSuperSpeed(int speed)
{
  return speed >= LIBUSB_SPEED_LOW && speed < LIBUSB_SPEED_SUPER;
}

inline bool isSuperSpeedOrHigher(int speed)
{
  return speed >= LIBUSB_SPEED_SUPER;
}

inline std::string formatInterfaceClaimError(int result)
{
  std::string message = formatLibusbError(result, true);
  if (result == LIBUSB_ERROR_BUSY)
  {
    message += ". The interface is owned by another process or driver; stop other Kinect, OpenNI, "
               "or Kinect SDK consumers and try again.";
  }
  return message;
}

} // namespace usb
} // namespace libfreenect2

#endif // LIBFREENECT2_USB_ERROR_H_
