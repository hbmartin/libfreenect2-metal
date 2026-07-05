# Depth accuracy and calibration

This page collects what is known about the accuracy of the depth values
libfreenect2 produces, and what you can do about it. It consolidates
long-standing upstream reports:
[#144](https://github.com/OpenKinect/libfreenect2/issues/144) (systematic
offset), [#319](https://github.com/OpenKinect/libfreenect2/issues/319)
(warped point clouds near corners),
[#596](https://github.com/OpenKinect/libfreenect2/issues/596) (calibration
accuracy), and [#865](https://github.com/OpenKinect/libfreenect2/issues/865)
(differences from the Microsoft SDK).

## What the numbers mean

Depth frames are 512x424 `float` values in **millimeters** along the IR
camera's optical axis (Z), not the ray length. `0` means no measurement
(invalidated by filters, out of range, or low confidence).

## Known systematic offset (~2 cm)

Careful measurements against a calibration target show depth values that are
consistently **~20-25 mm larger** than the true distance, roughly constant
across the image and across distances (#144). The suspected cause is the
processing chain's use of the device-provided x/z tables and phase-unwrapping
parameters, which do not perfectly reproduce whatever correction the
Microsoft SDK applies (#865). All GPU/CPU depth processors in libfreenect2
share the same math and tables, so the offset is the same regardless of the
`cpu`, `opengl`, `opencl`, `cuda`, or `metal` pipeline.

If your application needs absolute accuracy:

* Measure the offset for your unit against a known-distance flat target and
  subtract it. Per-device offsets between roughly 15 and 30 mm have been
  reported.
* Or perform a full external calibration (below), which absorbs the offset
  into the model.

## Warm-up drift

Time-of-flight sensors drift while the illuminator and sensor warm up. The
Kinect v2 typically reads a few millimeters differently during the first
**20-30 minutes** after power-on, then stabilizes
([#535](https://github.com/OpenKinect/libfreenect2/issues/535)). The onboard
fan cycling can also produce small steps in the bias. For metrology-style
use, let the device warm up before calibrating or measuring.

## Factory calibration limits

The intrinsics returned by `Freenect2Device::getIrCameraParams()` and
`getColorCameraParams()` are read from the device's factory calibration.
They are good enough for registration and casual use, but:

* The distortion model fits the image center better than the corners;
  point clouds of flat surfaces can bow by 1-2 cm near the edges (#319).
* Unit-to-unit variation exists; two Kinects will not agree exactly (#596).

For better results, calibrate the cameras yourself (a chessboard plus any
standard OpenCV-style intrinsic calibration works; the ROS
[iai_kinect2](https://github.com/code-iai/iai_kinect2) tooling automates
this for the Kinect v2) and construct `Registration` with your calibrated
parameters instead of the factory ones — both structs are plain values you
can fill in yourself.

## Intrinsics and rectification

`getIrCameraParams()` describes the **raw (distorted) IR/depth image**: `fx,
fy, cx, cy` are the pinhole parameters and `k1, k2, k3, p1, p2` the radial /
tangential distortion of that raw image
([#1083](https://github.com/OpenKinect/libfreenect2/issues/1083)).
`Registration::undistortDepth()` (or the `undistorted` output of
`Registration::apply()`) resamples the depth image so that the *same* `fx,
fy, cx, cy` apply as an ideal pinhole model with **no** distortion — that is
what `Registration::getPointXYZ()` assumes. Do not apply the distortion
coefficients to the undistorted image a second time.

## Differences from the Microsoft SDK

The Windows SDK produces slightly different depth values and XYZ coordinates
than libfreenect2 (#865): it uses proprietary lookup tables (including an IR
normalization table libfreenect2 does not have) and its own filtering.
Differences are typically small in the image center and grow toward the
edges. Comparisons between systems should account for this; neither output
is ground truth.
