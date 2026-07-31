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
