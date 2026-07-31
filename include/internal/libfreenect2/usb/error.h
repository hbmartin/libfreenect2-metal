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
  message << libusb_error_name(result) << " "
          << libusb_strerror(static_cast<libusb_error>(result));
  if (include_debug_hint)
    message << ". Set LIBUSB_DEBUG=3 in the process environment for more debug output.";
  return message.str();
}

} // namespace usb
} // namespace libfreenect2

#endif // LIBFREENECT2_USB_ERROR_H_
