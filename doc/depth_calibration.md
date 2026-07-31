# Per-device depth correction {#depth_calibration}

libfreenect2 exposes an opt-in linear correction for applications that need
better absolute Z accuracy:

```text
corrected_mm = scale * measured_mm + offset_mm
```

There is no universal Kinect v2 offset. Temperature, device calibration,
target geometry, distance, and the rest of the measurement setup all matter.
Profiles are tied to a device serial and firmware version, are never loaded by
a packet pipeline automatically, and do not modify raw recordings.

## Prepare the measurement

1. Let the Kinect warm up for 20-30 minutes. The command's `--warmup-frames`
   option only discards frames after a stream start; it does not replace
   thermal warm-up.
2. Use a flat, matte target large enough to cover the ROI. Keep it
   perpendicular to the depth camera and measure its optical-axis distance,
   not the distance along an off-axis ray.
3. Choose a central ROI that is entirely on the target. Avoid depth edges,
   reflective tape, and pixels that another object can occlude.
4. Keep the pipeline, filter settings, target, ROI, ambient conditions, and
   device temperature fixed across distances.

Each accepted frame contributes the median of its valid ROI pixels and its
median absolute deviation (MAD). Repeated frames at the same known distance
are reduced to their median before fitting, so a noisy frame or pixel does not
receive disproportionate weight.

## Fit from raw recordings

Raw recording inputs make a calibration reproducible and let you inspect the
same measurements later. Capture a bounded recording at each distance, then
fit them together:

```sh
KinectCapture record calibration-1000 --depth-frames 90
KinectCapture record calibration-2000 --depth-frames 90
KinectCapture record calibration-3000 --depth-frames 90

KinectDepthCalibration depth-profile.json \
  --roi 206 162 100 100 --frames 60 \
  --recording 1000 calibration-1000 \
  --recording 2000 calibration-2000 \
  --recording 3000 calibration-3000
```

The fitter replays depth through the CPU pipeline. It reads decoded frames but
does not rewrite the JPEG, raw depth packets, journal, or calibration in any
recording directory. All inputs must report the same serial and firmware.

## Fit from a live device

Specify each known distance with `--live`. The tool prompts before every
measurement so the target can be repositioned:

```sh
KinectDepthCalibration depth-profile.json \
  --serial 123456789012 --roi 206 162 100 100 \
  --warmup-frames 30 --frames 60 \
  --live 1000 --live 2000 --live 3000
```

One distinct known distance produces an offset-only model with `scale = 1`.
Two or more distinct distances fit both scale and offset from the
per-distance medians. The JSON profile records version, device identity, ROI,
model, coefficients, per-frame medians, MADs, valid sample counts, residuals,
and RMSE.

## Validate on an unseen distance

Do not judge a profile only on the distances used to fit it. Reserve at least
one distance as a holdout. A practical validation run is:

1. fit at three well-separated distances that bracket the working range;
2. capture a fourth distance that was not supplied to the fitter;
3. compute the same ROI median before and after correction;
4. compare absolute median error against the independently measured target
   distance.

For the v0.3 release gate, correction must reduce holdout median absolute
error by at least 25%. If it does not, inspect target alignment and MADs,
repeat after thermal stabilization, or retain the uncorrected data. A low
training RMSE does not override a failed holdout.

## Apply only when requested

`KinectCapture` applies a profile only when the option is present:

```sh
KinectCapture snapshot output --depth-correction depth-profile.json
```

The corrected depth is used for registration and the saved depth artifacts.
The metadata records the coefficients, RMSE, and whether the profile identity
matched the open device. A serial or firmware mismatch prints a warning and
is refused unless you explicitly add `--allow-device-mismatch`. That override
is intended for controlled experiments, not routine deployment. Raw
`KinectCapture record` mode never accepts or applies a correction profile.

Applications can perform the same explicit operation:

```cpp
libfreenect2::DepthCorrectionProfile profile;
std::string error;
if (!libfreenect2::DepthCorrectionProfile::load("depth-profile.json", profile, &error))
  throw std::runtime_error(error);

// Check profile.serial and profile.firmware against the open device first.
if (!profile.apply(depth_frame))
  throw std::runtime_error("depth frame is not decoded float data");
```

Invalid pixels (non-positive values, NaN, and infinity) are left unchanged.
The profile affects only the frame passed to `apply`; it never changes device
factory parameters or pipeline defaults.
