# MediaPipe Kinect exercise-pose demo

This developer tool runs MediaPipe Pose Landmarker on the Kinect v2 color
stream and uses libfreenect2 registration to attach camera-relative metric XYZ
coordinates to visible landmarks. The window shows the RGB skeleton, measured
front/side projections, and exercise joint angles. It does not classify posture
or provide medical or safety guidance.

## Build and set up

The prebuilt MediaPipe wheel officially targets Python 3.12. On Apple Silicon,
use a native arm64 Python and the same architecture for libusb, TurboJPEG, and
this project.

```sh
cmake -S . -B build -DBUILD_MEDIAPIPE_DEMO=ON -DENABLE_METAL=ON
cmake --build build --target mediapipe_kinect_bridge
tools/mediapipe_pose/setup_demo.sh
```

`setup_demo.sh` creates `tools/mediapipe_pose/.venv`, installs
`mediapipe==0.10.35`, and downloads the official Full FP16 model. The download
is accepted only when its SHA-256 is
`4eaa5eb7a98365221087693fcc286334cf0858e2eb6e15b506aa4a7ecdcec4ad`.

## Run

```sh
tools/mediapipe_pose/.venv/bin/python tools/mediapipe_pose/pose_demo.py
```

The demo locates `build/lib/libmediapipe_kinect_bridge.dylib` automatically.
Useful options include:

```text
--pipeline auto|metal|cpu
--serial KINECT_SERIAL
--bridge /path/to/libmediapipe_kinect_bridge.dylib
--model /path/to/another_pose_landmarker.task
--output captures/my_session
--visibility 0.6
--presence 0.6
```

Controls are `Q` or Escape to quit, Space to pause, `R` to toggle JSONL session
recording, and `S` to save an annotated PNG plus matching JSON record. Recording
is opt-in. Outputs default to an ignored `captures/mediapipe_pose_<timestamp>`
directory.

Green landmarks have Kinect-measured XYZ. Orange landmarks are model-only, and
gray landmarks are below the confidence threshold. An angle is labeled
`kinect` only when all required joints have measured depth. Otherwise it is
computed entirely from MediaPipe world landmarks and labeled `model`; the demo
never mixes the coordinate systems within one angle.

For full-body exercise measurements, keep one person centered with their head
and feet visible. Kinect RGB and depth exposure are not simultaneous; metric
lifting is suppressed when their device timestamps differ by more than 50 ms.

## Tests

The pure Python logic does not import MediaPipe or require a Kinect:

```sh
python3 -m unittest tests/test_mediapipe_pose.py
```

Native reverse-map and depth-selection tests are part of the regular CTest
suite when `BUILD_TESTING=ON`.
