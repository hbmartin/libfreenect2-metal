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

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include <libfreenect2/timing.h>

TEST(Timing, MonotonicClockNeverMovesBackward)
{
  uint64_t previous = libfreenect2::monotonicTimeMicroseconds();
  for(int i = 0; i < 1000; ++i)
  {
    const uint64_t current = libfreenect2::monotonicTimeMicroseconds();
    EXPECT_GE(current, previous);
    previous = current;
  }
}

TEST(Timing, MonotonicClockReportsMicroseconds)
{
  const uint64_t before = libfreenect2::monotonicTimeMicroseconds();
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  const uint64_t after = libfreenect2::monotonicTimeMicroseconds();
  EXPECT_GE(after - before, 1000u);
}

TEST(Timing, DeviceTimestampDeltaCrossesWrapSafely)
{
  EXPECT_EQ(libfreenect2::deviceTimestampDelta(2u, UINT32_MAX - 1u), 4);
  EXPECT_EQ(libfreenect2::deviceTimestampDelta(UINT32_MAX - 1u, 2u), -4);
  EXPECT_EQ(libfreenect2::deviceTimestampDistance(2u, UINT32_MAX - 1u), 4u);
  EXPECT_TRUE(libfreenect2::deviceTimestampBefore(UINT32_MAX - 1u, 2u));
  EXPECT_FALSE(libfreenect2::deviceTimestampBefore(2u, UINT32_MAX - 1u));
}

TEST(Timing, DeviceTimestampSpanHandlesWraparound)
{
  const uint32_t timestamps[] = {UINT32_MAX - 2u, 1u, 4u};
  EXPECT_EQ(libfreenect2::deviceTimestampSpan(timestamps, 3), 7u);
  EXPECT_EQ(libfreenect2::deviceTimestampSpan(timestamps, 1), 0u);
  EXPECT_EQ(libfreenect2::deviceTimestampSpan(0, 0), 0u);
}
