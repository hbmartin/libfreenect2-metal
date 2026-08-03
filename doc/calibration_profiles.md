# Conventional camera calibration profiles {#calibration_profiles}

[TOC]

Version 0.4 adds an OpenCV-free runtime representation for conventional color
and IR camera models, their rigid transform, optional scalar depth correction,
quality measurements, and provenance. This complements the Kinect factory
polynomial mapping used by `Registration`; it does not replace it.

## Canonical JSON profile

`CalibrationProfile` reads and writes schema
`libfreenect2.calibration-profile`, version 1. The file contains:

- the Kinect serial and firmware that produced the observations;
- color and IR resolution, pinhole intrinsics, and either Brown-Conrady 5 or
  rational 8 distortion coefficients;
- a row-major orthonormal depth-to-color rotation and translation in meters;
- an optional offset or linear depth correction in millimeters;
- optional view counts and intrinsic, stereo, and depth validation errors;
- UTC creation time, tool version, and the calibration-job SHA-256.

JSON parsing rejects non-finite numbers, invalid resolutions or focal lengths,
unsupported distortion models, and non-rigid transforms. Unknown object fields
are ignored so producers can add diagnostics without changing the schema.
Writes use a sibling temporary file and atomic rename.

Profiles are device-bound. `matchesDevice()` rejects a different serial unless
the caller explicitly opts in. A firmware difference is reported as a warning:
it is useful evidence, but does not prove the geometric calibration is wrong.

## Runtime registration

`ProjectiveRegistration` maps decoded float depth into any validated target
camera model. It undistorts each IR source ray, applies the depth-to-color rigid
transform, distorts into the target raster, and resolves collisions with a
deterministic nearest-Z rule. Missing output pixels are zero.

Two rasterizers are available:

- `Nearest` writes the closest target pixel and preserves the sparsest result.
- `FourNeighborSplat` writes the four surrounding target pixels to reduce
  holes. It is the default.

The source and output frames must use `Frame::Float` and match their declared
camera resolutions. Depth correction is disabled by default even if the profile
contains one; set `ProjectiveRegistrationOptions::apply_depth_correction` only
when the input depth has not already been corrected. A registration object is
immutable after construction and can be shared between threads when each call
uses a distinct output frame.

`KinectCapture snapshot OUTPUT --calibration-profile profile.json` demonstrates
the API. It retains the factory `registered.ppm` and additionally writes
`registered_depth_mm.pgm` and `registered_depth_false_color.ppm` in color-camera
geometry.

## Building the offline tool

The headless calibration executable is opt-in and does not add OpenCV to the
runtime library:

```sh
cmake -S . -B build-calibration -G Ninja \
  -DBUILD_CALIBRATION_TOOLS=ON -DBUILD_TESTING=ON
cmake --build build-calibration --target KinectCameraCalibration
```

OpenCV 4.5 or newer is required. OpenCV 4's `calib3d` and OpenCV 5's renamed
`calib` module are both supported. The tool has no GUI and consumes durable
libfreenect2 recording directories.

Download the [example calibration job](calibration_job.example.json) and list
recordings by role. Paths are resolved relative to the job file. `columns` and
`rows` are the number of inner
chessboard corners; `square_size_mm` must be measured, not taken from a printer
dialog. Version 0.4 supports chessboards only.

```sh
# Verify schema, paths, gates, and show the canonical job SHA-256.
KinectCameraCalibration inspect --job calibration-job.json

# Staged workflow. The state file contains observations and the solved result,
# so detection does not need to be repeated while tuning gates.
KinectCameraCalibration detect --job calibration-job.json --state observations.json
KinectCameraCalibration solve --job calibration-job.json --state observations.json
KinectCameraCalibration validate --job calibration-job.json \
  --state observations.json --output profile.json

# Or run all three stages.
KinectCameraCalibration run --job calibration-job.json \
  --state observations.json --output profile.json
```

Detection accepts only sufficiently different successive board poses. Solving
uses deterministic training/holdout partitions and median/MAD rejection,
calibrates both intrinsics and their stereo transform, estimates board distance
from IR pose, and compares offset-only with linear depth correction on holdout
observations. Console output reports accepted views and final RMS metrics.

The balanced defaults require 20 usable views in every role, intrinsic RMS at
most 1.0 px, stereo/held-out RMS at most 1.5 px, and depth RMSE at most 20 mm.
A failed gate withholds the profile. `--allow-low-quality` is an explicit escape
hatch for investigation; it prints a warning and should not be used for
production calibration.

## Legacy kinect2_calibration YAML

Four-file OpenCV YAML sets used by `kinect2_bridge` can be converted without
changing the runtime dependency boundary:

```sh
KinectCameraCalibration import-yaml --input-dir CALIB_DIR \
  --serial KINECT_SERIAL --firmware VERSION --output profile.json
KinectCameraCalibration export-yaml --profile profile.json --output-dir CALIB_DIR
```

Legacy YAML can represent an offset (`depthShift`) but not a non-unit linear
depth scale; export rejects that lossy conversion. Imported files do not carry
quality measurements and should be validated against held-out observations.

## Recording attachment

`RecordingWriter` outputs use manifest version 2 when a profile is attached
and version 1 otherwise. After publishing factory calibration, call
`setCalibrationProfile()` to store the canonical file at
`calibration/profile.json`. `KinectCapture record` exposes this as
`--calibration-profile`. Replay validates the referenced file before opening
and accepts a serial mismatch (possible only through the writer's explicit
`allow_serial_mismatch` opt-in); `getCalibrationProfile()` returns a copy.
Readers remain compatible with manifest version 1 recordings.
