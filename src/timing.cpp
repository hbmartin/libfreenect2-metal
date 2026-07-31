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
#include <climits>

namespace libfreenect2
{

uint64_t monotonicTimeMicroseconds()
{
  const std::chrono::steady_clock::duration elapsed =
      std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
}

int32_t deviceTimestampDelta(uint32_t timestamp, uint32_t reference)
{
  const uint32_t raw = timestamp - reference;
  const int64_t delta = raw <= static_cast<uint32_t>(INT32_MAX)
                            ? static_cast<int64_t>(raw)
                            : static_cast<int64_t>(raw) - (INT64_C(1) << 32);
  return static_cast<int32_t>(delta);
}

uint32_t deviceTimestampDistance(uint32_t first, uint32_t second)
{
  const int64_t delta = deviceTimestampDelta(first, second);
  return static_cast<uint32_t>(delta < 0 ? -delta : delta);
}

bool deviceTimestampBefore(uint32_t first, uint32_t second)
{
  return first != second && deviceTimestampDelta(first, second) < 0;
}

uint32_t deviceTimestampSpan(const uint32_t* timestamps, size_t count)
{
  if(timestamps == 0 || count == 0)
    return 0;

  int64_t minimum = 0;
  int64_t maximum = 0;
  for(size_t i = 1; i < count; ++i)
  {
    const int64_t delta = deviceTimestampDelta(timestamps[i], timestamps[0]);
    if(delta < minimum)
      minimum = delta;
    if(delta > maximum)
      maximum = delta;
  }

  const uint64_t span = static_cast<uint64_t>(maximum - minimum);
  return span > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(span);
}

} // namespace libfreenect2
