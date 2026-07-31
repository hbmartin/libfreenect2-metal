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

/** @file timing.h Internal host and device timestamp helpers. */

#ifndef LIBFREENECT2_TIMING_H_
#define LIBFREENECT2_TIMING_H_

#include <cstdint>

namespace libfreenect2
{

/** Monotonic host time in microseconds from an unspecified steady epoch. */
uint64_t monotonicTimeMicroseconds();

} // namespace libfreenect2

#endif // LIBFREENECT2_TIMING_H_
