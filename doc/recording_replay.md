# Recording, replay, and multiple Kinects {#recording_replay}

## What exists today

Recording and replay have been requested for years
([#438](https://github.com/OpenKinect/libfreenect2/issues/438),
[#442](https://github.com/OpenKinect/libfreenect2/issues/442),
[#948](https://github.com/OpenKinect/libfreenect2/issues/948)); the pieces
that landed are:

### Raw recording: the `dump` pipeline

`DumpPacketPipeline` (`LIBFREENECT2_PIPELINE=dump`) delivers the *raw*
compressed packets instead of decoded images: color frames are the JPEG
bitstream as `Frame::Raw`, depth frames are the raw 11-bit phase packets.
Write these buffers to files and you have a lossless recording at minimal
CPU cost.

### Replay: `Freenect2Replay`

`Freenect2Replay::openDevice(filenames)` creates a virtual device that runs
recorded raw frames through any processing pipeline — the same API as a
real device (start, listeners, registration), no Kinect attached. Filenames
must follow `<prefix>_<timestamp>_<sequence>.<suffix>` where the suffix is
`.depth` (raw depth packet, exactly 2,984,960 bytes) or `.jpg`/`.jpeg`
(color JPEG). Because depth is reprocessed on replay, you can re-run
recordings through a different or newer pipeline (e.g. `metal`) at full
quality.

### Video streaming/recording: `tools/streamer_recorder`

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
