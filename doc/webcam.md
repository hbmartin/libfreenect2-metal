# Using the Kinect v2 as a webcam {#webcam}

A recurring question
([#1162](https://github.com/OpenKinect/libfreenect2/issues/1162),
[#1144](https://github.com/OpenKinect/libfreenect2/issues/1144),
[#1151](https://github.com/OpenKinect/libfreenect2/issues/1151),
[#1175](https://github.com/OpenKinect/libfreenect2/issues/1175)):
Protonect works, but the Kinect does not show up as a camera in OBS, Zoom,
FaceTime, browsers, etc.

**libfreenect2 is a sensor library, not a camera driver.** It talks to the
Kinect over libusb from your application's process. Operating systems
discover webcams through their camera driver frameworks (AVFoundation /
CoreMediaIO on macOS, V4L2 on Linux, Media Foundation on Windows), and
nothing in libfreenect2 registers there. Installing libfreenect2 will never,
by itself, make the Kinect appear in the system camera list.

To feed Kinect frames into apps that expect a webcam you need a bridge that
takes frames from a libfreenect2 program and republishes them as a virtual
camera:

## macOS

* **OBS virtual camera**: write (or find) a small app that renders the RGB
  stream, capture that window in OBS, and enable *Start Virtual Camera*.
  This is the lowest-effort path today.
* **Syphon**: publish frames to [Syphon](https://syphon.github.io/) from
  your libfreenect2 app; OBS and many video tools can ingest Syphon
  sources.
* **CoreMediaIO Camera Extension**: the proper modern solution is a
  [Camera Extension](https://developer.apple.com/documentation/coremediaio)
  that owns the device and republishes frames system-wide. This is a
  planned direction for this fork; contributions welcome. (The legacy
  CoreMediaIO DAL plugin API it replaces is deprecated and no longer loads
  in most apps.)

## Linux

* Feed frames into a [v4l2loopback](https://github.com/umlaeute/v4l2loopback)
  device, e.g. from a small program or a GStreamer pipeline (see upstream
  [#1178](https://github.com/OpenKinect/libfreenect2/issues/1178)). Apps
  then see a normal `/dev/videoN` camera.

## Windows

* OBS's virtual camera, or any DirectShow/Media Foundation virtual camera
  SDK, fed from a libfreenect2 capture program.

Note that only the 1920x1080 RGB stream makes sense as a webcam; depth and
IR streams have no standard camera representation (some bridges publish
them as grayscale video).
