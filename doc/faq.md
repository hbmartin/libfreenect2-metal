# Frequently asked questions

Answers to recurring API questions from the upstream tracker.

## Why are the images mirrored, and how do I un-mirror them? ([#172](https://github.com/OpenKinect/libfreenect2/issues/172))

All streams (color, IR, depth) are horizontally mirrored relative to the
Microsoft SDK's output — you see the scene as if looking into a mirror.
This is consistent across streams, so registration and `getPointXYZ()`
remain self-consistent; there is no API option to change it. If you need
SDK-style orientation, flip the final images yourself, e.g.
`cv::flip(img, img, 1)`, or negate X when exporting point clouds. Flip at
the end of your pipeline: flipping *before* registration would break the
mapping.

## Can I switch off the IR emitter? ([#338](https://github.com/OpenKinect/libfreenect2/issues/338))

No. Unlike the Kinect v1, no protocol command is known that disables the
Kinect v2's illuminator while streaming; the emitter is controlled by the
firmware together with the depth stream. Physically covering the emitters
kills depth measurements (the IR image remains usable with ambient IR).
If you only need the color stream, note that `startStreams(rgb=true,
depth=false)` still powers the sensor bar as the firmware dictates.

## How do I silence the [Info] log spam? ([#1058](https://github.com/OpenKinect/libfreenect2/issues/1058))

Two ways:

* Environment variable, no code change:
  `LIBFREENECT2_LOGGER_LEVEL=warning ./bin/Protonect`
  (accepted values: `debug`, `info`, `warning`, `error`, `none`).
* In code, install your own logger or a quieter console logger **before**
  creating `Freenect2`:

  ```cpp
  libfreenect2::setGlobalLogger(
      libfreenect2::createConsoleLogger(libfreenect2::Logger::Warning));
  // or setGlobalLogger(NULL); to disable logging entirely
  ```

## What do Frame::timestamp values mean? ([#792](https://github.com/OpenKinect/libfreenect2/issues/792), [#869](https://github.com/OpenKinect/libfreenect2/issues/869))

`Frame::timestamp` is the **device's** clock, in ticks of 0.125 ms
(so it advances by ~266 per frame at 30 Hz, ~533 at 15 Hz in low light).
It is not wall-clock time and it resets when the device restarts. To
correlate with host time, record `now()` when the frame arrives and fit an
offset (the USB delivery jitter is a few milliseconds); the library does
not do this for you.

Multiply by `0.125f` to get milliseconds:
`double ms = frame->timestamp * 0.125;`

## How well are color and depth synchronized? ([#721](https://github.com/OpenKinect/libfreenect2/issues/721))

Color and depth frames carry timestamps from the same device clock, and
`SyncMultiFrameListener` pairs the frames that arrive together, but the two
cameras expose independently: the color camera auto-exposes (see
`Frame::exposure`) while the depth stream uses fixed short IR exposures.
Under low light the color stream can drop to 15 Hz while depth stays at
30 Hz, so a "synchronized" pair can be up to one frame interval apart. For
motion-sensitive work, match frames by timestamp difference rather than
arrival order, and prefer good lighting so RGB stays at 30 Hz.
