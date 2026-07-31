# Frame timing and software pairing {#frame_timing}

Kinect v2 frames expose two clocks with different purposes:

* `Frame::timestamp` is the device timestamp in 0.125 ms ticks. It is useful
  for comparing color, IR, and depth frames from one device. The 32-bit value
  wraps about every 6.2 days and resets when the device restarts.
* `Frame::arrival_timestamp_us` is a monotonic host timestamp captured at the
  callback entry for the first completed USB transfer contributing to the
  frame. It measures delivery timing without wall-clock jumps. Its epoch is
  unspecified, so store a separate wall-clock observation if recordings must
  be related to UTC. Replay frames use their actual replay-delivery time.

`Frame::sequence` identifies stream order, but sequence values from different
streams are not a synchronization contract. Device timestamps from different
Kinects also do not share an epoch.

## Pairing is not hardware synchronization

`SyncMultiFrameListener` preserves the original arrival-based behavior: it
collects one frame of every requested type and replaces an earlier frame when
another frame of that type arrives before the set is complete. It does not
test timestamps. The word "sync" in its name means that the API returns the
requested frame types together, not that their exposures happened together.

`TimestampAlignedFrameListener` performs stricter software pairing. It keeps
a bounded queue for every requested stream, searches the queued combinations
for the smallest wrap-safe device-timestamp span, and delivers a set only when
that span is at or below `max_delta_ticks`. Queue overflow and frames skipped
before a selected combination count as drops. `getStatistics()` returns a
thread-safe snapshot containing delivered-set and dropped-frame counts plus
the last and maximum delivered deltas.

Neither listener controls exposure timing. The RGB camera auto-exposes and can
fall to 15 Hz in low light while the IR/depth stream continues at 30 Hz.
Software pairing cannot trigger the two cameras simultaneously, synchronize
separate Kinect devices, or recover a frame that was never transferred. Use
good lighting, an explicit timestamp threshold, and the drop statistics when
motion alignment matters. Kinect v2 does not expose a supported external
trigger through libfreenect2.

## Choosing a threshold

The threshold uses device ticks, where 200 ticks equals 25 ms:

```cpp
const unsigned int types =
    libfreenect2::Frame::Color | libfreenect2::Frame::Depth;
libfreenect2::TimestampAlignedFrameListener listener(types, 200, 8);

libfreenect2::FrameMap frames;
if (listener.waitForNewFrame(frames, 1000))
{
  // The selected timestamps have a wrap-safe span of at most 200 ticks.
  listener.release(frames);
}
```

Start with a threshold that reflects the maximum motion error your application
can tolerate, then inspect the observed deltas and drops. A smaller threshold
improves temporal proximity but can increase latency and drops. A larger queue
can find better combinations during jitter, but holds more frame memory and
does not compensate for sustained rate differences.

Do not subtract unsigned device timestamps and apply `abs()`: that fails at
wraparound. `TimestampAlignedFrameListener` performs wrap-safe comparisons
internally.

## Snapshot diagnostics

The one-argument capture command retains legacy pairing:

```sh
KinectCapture OUTPUT_DIRECTORY
```

Use explicit snapshot mode to enforce a threshold. Its default is 200 ticks
(25 ms), and it can be overridden:

```sh
KinectCapture snapshot OUTPUT_DIRECTORY
KinectCapture snapshot OUTPUT_DIRECTORY --max-delta-ticks 80
```

The command reports delivered sets, dropped frames, and observed deltas. Its
`metadata.json` records the device timestamp, arrival timestamp, and sequence
for color, IR, and depth, along with the alignment threshold and statistics.

See upstream discussions
[#721](https://github.com/OpenKinect/libfreenect2/issues/721),
[#792](https://github.com/OpenKinect/libfreenect2/issues/792), and
[#869](https://github.com/OpenKinect/libfreenect2/issues/869) for the original
timing and synchronization requests.
