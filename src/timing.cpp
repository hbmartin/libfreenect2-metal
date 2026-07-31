/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * Copyright (c) 2014 individual OpenKinect contributors. See the CONTRIB file
 * for details.
 *
 * This code is licensed to you under the terms of the Apache License, version
 * 2.0, or, at your option, the terms of the GNU General Public License,
 * version 2.0. See the APACHE20 and GPL2 files for the text of the licenses.
 */

#include <libfreenect2/timing.h>

#include <chrono>

namespace libfreenect2
{

uint64_t monotonicTimeMicroseconds()
{
  const std::chrono::steady_clock::duration elapsed =
      std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
}

} // namespace libfreenect2
