# Using libfreenect2 from Python {#python}

[TOC]

The maintained Python interface for this fork is
[pylibfreenect3](https://github.com/hbmartin/pylibfreenect3). It binds the
0.3 device, recording, alignment, registration, and vision APIs without a
ctypes bridge.

Its examples include an aligned OpenCV viewer and a complete MediaPipe pose
demo with reusable registration buffers and metric landmark lifting. OpenCV
and MediaPipe remain example-only dependencies.

## Pose-estimation workflow

libfreenect2 has no native body-tracking or skeleton-frame API. The maintained
Python route is:

1. Capture synchronized color and depth frames with pylibfreenect3.
2. Build the registration maps and undistorted depth image once per frame.
3. Run MediaPipe pose inference on the color image.
4. Lift each normalized color landmark through the color-to-depth map into
   metric XYZ, retaining the per-landmark validity result when no coherent
   depth sample can be found.

The pose coordinates produced by this workflow are MediaPipe model estimates
anchored to measured depth. They are not Kinect firmware output, ground-truth
joints, or an equivalent of the Kinect SDK's tracked-body records. Applications
must handle missing depth and model uncertainty rather than interpreting every
landmark as a sensor-confirmed joint.

The legacy native MediaPipe demo previously hosted in this repository has
moved there so capture, registration, and Python ownership semantics have one
canonical implementation.
