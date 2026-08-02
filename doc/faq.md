# Frequently asked questions {#faq}

[TOC]

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

## Can libfreenect2 return body or skeleton frames? ([#1199](https://github.com/OpenKinect/libfreenect2/issues/1199))

No. libfreenect2 returns color, infrared, and depth frames; it does not
implement the Kinect SDK's body tracker or its 25-joint skeleton frame.

The supported pose workflow for this fork is to capture aligned frames through
[pylibfreenect3](https://github.com/hbmartin/pylibfreenect3), run MediaPipe on
the color image, and lift landmarks with valid registered depth into metric XYZ
coordinates. See @ref python. Those joints are estimates from a machine-learning
model combined with measured depth. They are not sensor-provided ground truth,
and they do not carry the Kinect SDK's tracking states or body identities.

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
It is not wall-clock time, wraps as a 32-bit value, and resets when the device
restarts. `Frame::arrival_timestamp_us` records the monotonic host time of the
first contributing USB transfer. It has no wall-clock epoch; sample your wall
clock separately if you need UTC correlation.

Multiply by `0.125f` to get milliseconds:
`double ms = frame->timestamp * 0.125;`

## How well are color and depth synchronized? ([#721](https://github.com/OpenKinect/libfreenect2/issues/721))

Color and depth frames carry timestamps from the same device clock, but the
two cameras expose independently. `SyncMultiFrameListener` groups requested
frame types without enforcing a timestamp delta; it does not provide hardware
synchronization. For motion-sensitive work, use
`TimestampAlignedFrameListener` with an explicit threshold and inspect its
drop/delta statistics. See the @ref frame_timing guide for clock semantics,
wraparound, listener behavior, and capture diagnostics.

## Can I get color and IR at the same time?

Yes. Color arrives on a bulk endpoint and IR/depth on an isochronous endpoint,
and the two run concurrently — request
`Frame::Color | Frame::Ir | Frame::Depth` from a single listener.

This is worth stating because the answer for the **Kinect v1** was no: there,
color and IR were two modes of one stream and you could have only one at a
time. If you found advice to that effect, it does not apply here. See
@ref kinect_v1_vs_v2.

## Does libfreenect2 do skeleton or body tracking?

No. This is a sensor driver: it delivers color, IR, and depth frames, plus
registration between them. There is no skeleton, joint, or body-index API, and
adding one is out of scope.

For pose estimation, the maintained route is MediaPipe on the color image with
landmarks lifted into metric coordinates through the registration maps — see
@ref python, which walks through the whole workflow. The OpenNI2 driver
(`BUILD_OPENNI2_DRIVER`) is another option if you need to feed an existing
OpenNI/NiTE-based middleware stack.

## Can I tilt the sensor or read its accelerometer?

No, because the hardware has neither. The Kinect v2's base is fixed; the tilt
motor and accelerometer belong to the Kinect v1. Aim it by moving the sensor.

The v2 does have two controllable status LEDs — `Freenect2Device::setLedStatus()`
takes an intensity from 0 to 1000 and a constant or blink mode. See
@ref protocol.

## Can I use the microphone array?

Not through libfreenect2. The sensor exposes a four-microphone array on a
separate USB interface association, and this library does not implement it.
Calibrated directional audio is one of the features this fork explicitly does
not provide.

On **Linux** the kernel handles it for you: recent kernels bind
`snd-usb-audio` to those interfaces, so the array shows up as a normal ALSA
capture device without any help from this library. Upstream reports it
enumerating as 16 kHz, 4 channels, `S32_LE`:

```sh
arecord -L                                                   # find the device
arecord -t wav -r 16000 -c 4 -f S32_LE -D hw:CARD=Sensor test.wav
```

This project does not test the audio path, and the channel layout is not
documented anywhere authoritative — treat the parameters above as a starting
point rather than a specification. There is no equivalent on macOS or Windows.

## Why does the depth image have holes and shadows?

Several distinct causes, which is why the holes do not all look alike:

* **Parallax shadow.** The IR emitter and the IR sensor sit a few centimetres
  apart, so a foreground object occludes illumination from the sensor's view.
  These are hard-edged and always fall on the same side of an object.
* **Out of range.** Anything outside `Config::MinDepth`/`MaxDepth` (0.5–4.5 m
  by default) is invalidated. See @ref configuration.
* **Too little returned light.** Dark, matte, or steeply angled surfaces reflect
  too little IR to measure. Mirrors and glass return light from the wrong place
  entirely.
* **Filtering.** The bilateral and edge-aware filters invalidate low-confidence
  pixels, particularly at depth discontinuities. Both can be turned off if you
  would rather do your own filtering — again, @ref configuration.

Invalid pixels read as `0`, not `2047`; the `2047` sentinel is a Kinect v1
convention.

## Do two Kinect v2 sensors interfere with each other?

They can, and the mechanism is different from the Kinect v1's. The v1 projected
a fixed speckle pattern, so two units overlaid two patterns and confused each
other's matching. The v2 measures the phase of amplitude-modulated IR, so a
second unit's illumination adds unmodelled light to the first unit's
measurement, which shows up as noise and outliers in the overlap region rather
than as an outright failure.

In practice, overlapping views degrade gracefully rather than break, and how
much depends on geometry and overlap. This project does not characterize it. Do
not carry over v1-era advice about polarizing filters — it addressed a
mechanism the v2 does not have.

Note that the far more common multi-sensor problem is USB bandwidth, not
optical interference: each sensor needs its own host controller. See
@ref linux_usb and @ref recording_replay.
