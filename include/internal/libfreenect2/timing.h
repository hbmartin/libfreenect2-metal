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
#include <cstddef>

namespace libfreenect2
{

/** Monotonic host time in microseconds from an unspecified steady epoch. */
uint64_t monotonicTimeMicroseconds();

/** Signed shortest delta from @p reference to @p timestamp on the wrapping
 * 32-bit Kinect device clock. Values exactly half a clock apart are negative. */
int32_t deviceTimestampDelta(uint32_t timestamp, uint32_t reference);

/** Absolute wrap-safe distance between two Kinect device timestamps. */
uint32_t deviceTimestampDistance(uint32_t first, uint32_t second);

/** True when @p first precedes @p second within the device clock half-range. */
bool deviceTimestampBefore(uint32_t first, uint32_t second);

/** Smallest span containing timestamps that are mutually within one device
 * clock half-range. Returns UINT32_MAX for an unrepresentable/ambiguous set. */
uint32_t deviceTimestampSpan(const uint32_t* timestamps, size_t count);

} // namespace libfreenect2

#endif // LIBFREENECT2_TIMING_H_
