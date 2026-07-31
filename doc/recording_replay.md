# Recording, replay, and multiple Kinects {#recording_replay}

## Version 1 recording directories

Recording and replay were requested for years
([#438](https://github.com/OpenKinect/libfreenect2/issues/438),
[#948](https://github.com/OpenKinect/libfreenect2/issues/948)). Version 0.3
adds a durable, self-contained directory format for the raw Kinect streams:

```text
recording/
  manifest.json
  frames.ndjson
  calibration/p0.bin
  frames/color/0000000000.jpg
  frames/depth/0000000001.depth
  recording.complete
```

`manifest.json` has format version 1. It records the device serial and
firmware, JPEG and Kinect-v2-raw stream encodings, color and IR camera
parameters, the safe relative path to the P0 tables, and the clock semantics.
Device timestamps are the wrapping Kinect clock in 0.125 ms ticks. Arrival
offsets are monotonic host microseconds relative to writer construction.

`frames.ndjson` is an append-only journal. Every newline-terminated object has
a global index, stream, relative path, byte count, device timestamp, sequence,
arrival offset, and (for color) exposure, gain, and gamma. Global indices define
the cross-stream replay order.

### Durability and recovery

Each frame and metadata file is first written to a sibling `.part` file,
closed, and atomically renamed. Only then is its journal entry appended and
flushed. `recording.complete` is published last, after the frame journal,
calibration, and manifest close successfully. A process interruption therefore
leaves an obviously incomplete directory rather than a falsely complete one.

Replay rejects a missing or empty completion marker by default. Explicit
`ReplayOptions::salvage_incomplete` mode ignores only a truncated final journal
fragment. Every earlier line must still parse, indices must be contiguous, paths
must be relative without empty, `.` or `..` components, extensions must match
their streams, and every file size must equal the journaled byte count. Missing
calibration and unsupported manifest versions always fail, including salvage
mode.

## Capturing

`RecordingWriter` is a bounded, thread-safe `FrameListener` for the raw dump
pipeline. It copies callbacks into an asynchronous queue, reports written and
dropped frame counts, stores P0 calibration, and publishes the completion marker
only from a clean `close()`. Install it before starting streams, then call
`getCalibrationData()` and `setCalibration()` immediately after startup.

The bundled hardware capture command implements that sequence and requires an
explicit bound:

```text
KinectCapture record OUTPUT_DIRECTORY --depth-frames 1800
KinectCapture record OUTPUT_DIRECTORY --duration-seconds 60
```

The output directory must not already exist. An interrupt leaves it incomplete
and available for explicit salvage inspection.

### Raw packet representation

`DumpPacketPipeline` (`LIBFREENECT2_PIPELINE=dump`) delivers the *raw*
compressed packets instead of decoded images: color frames are the JPEG
bitstream as `Frame::Raw`, depth frames are the raw 11-bit phase packets.
Write these buffers to files and you have a lossless recording at minimal
CPU cost.

## Replaying a recording directory

`Freenect2Replay::openRecording(directory, options)` validates the manifest,
calibration, completion state, journal, paths, and byte counts before it creates
a virtual device. An overload accepts a selected `PacketPipeline`. The replay
device reports the recorded serial and firmware and supports color-only,
depth-only, or combined starts without invoking an unrequested decoder.

Fast mode is the default and preserves global journal order without sleeping.
Set `ReplayOptions::reproduce_timing` to schedule frames from their recorded
arrival offsets. Those waits are interruptible by `stop()`; delivered
`Frame::arrival_timestamp_us` values are always the actual replay-delivery
monotonic time, not a copied historical host clock.

Raw depth recordings can be replayed through CPU, Metal, or another compatible
pipeline. The manifest IR parameters and raw P0 tables are sufficient to rebuild
the derived X, Z, and lookup tables.

## Legacy filename-list replay

`Freenect2Replay::openDevice(filenames)` creates a virtual device that runs
recorded raw frames through any processing pipeline — the same API as a
real device (start, listeners, registration), no Kinect attached. Filenames
must follow `<prefix>_<timestamp>_<sequence>.<suffix>` where the suffix is
`.depth` (raw depth packet, exactly 2,984,960 bytes) or `.jpg`/`.jpeg`
(color JPEG). Because depth is reprocessed on replay, you can re-run
recordings through a different or newer pipeline (e.g. `metal`) at full
quality.

Depth decoding requires the device calibration that was active during
recording. `Freenect2Replay::Calibration` carries color and IR camera
parameters plus validated P0, X, Z, and lookup tables. Pass it to the
corresponding `openDevice()` overload. Incomplete or incorrectly sized table
sets are rejected instead of reaching a packet processor in a partially ready
state. Color-only loose-file replay does not require calibration.

These filename-list overloads remain unchanged and independent of
`ReplayOptions`; they are useful for existing loose-file applications. New code
should prefer `openRecording()` so identity, timestamps, calibration, validation,
and recovery policy travel with the data.

## Video streaming/recording: `tools/streamer_recorder`

A contributed Protonect variant that records decoded streams or streams
them over a socket. Enable with `-DBUILD_STREAMER_RECORDER=ON` (requires
OpenCV; see `tools/streamer_recorder/README.md`).

For conventional video files or ROS-style workflows, capture decoded frames
yourself (OpenCV `VideoWriter`, ffmpeg pipe — see
[#1073](https://github.com/OpenKinect/libfreenect2/issues/1073)); a
built-in FFmpeg device remains an open feature request.

## Multiple Kinects

([#688](https://github.com/OpenKinect/libfreenect2/issues/688),
[#715](https://github.com/OpenKinect/libfreenect2/issues/715),
[#1186](https://github.com/OpenKinect/libfreenect2/issues/1186))

The library supports any number of devices: `enumerateDevices()`, then
`openDevice(idx_or_serial)` for each, with one `SyncMultiFrameListener`
per device. The constraints are hardware:

* **One Kinect per USB3 host controller** is the safe rule. A Kinect v2
  saturates most of a USB3 controller's bandwidth; two on the same
  controller usually means dropped transfers and stalled streams, hubs
  make it worse.
* On **Apple Silicon Macs**, each Thunderbolt/USB4 port generally has its
  own controller, so plugging each Kinect into a different physical port
  (not a hub) typically works; check `system_profiler SPUSBDataType` to
  confirm the topology.
* On Linux, also raise the usbfs memory limit (see the
  [troubleshooting wiki](https://github.com/OpenKinect/libfreenect2/wiki/Troubleshooting#multiple-kinects-try-increasing-usbfs-buffer-size));
  this limit does not exist on macOS.
* IR interference between overlapping Kinect v2 views is minor (each unit's
  time-of-flight modulation tolerates others surprisingly well), but
  depth noise does increase where illuminators overlap strongly.
