# Kinect still-frame capture

`KinectCapture` saves a synchronized Kinect v2 frame set without requiring an
OpenGL viewer. It exports full-resolution RGB, infrared, raw millimeter depth,
false-color depth, color registered into depth-camera coordinates, and capture
metadata.

The executable is built with the other examples:

```sh
cmake -S . -B build
cmake --build build --target KinectCapture
mkdir -p captures/example
./build/bin/KinectCapture captures/example
```

On a Metal-enabled build the tool uses the Metal depth pipeline. Other builds
fall back to the CPU pipeline. It discards the first 19 synchronized frames so
the color camera has time to settle before exporting the twentieth frame.

The output images use portable Netpbm formats (`.ppm` and `.pgm`) and can be
opened by common image tools. `depth_mm.pgm` stores the depth value directly as
a big-endian 16-bit millimeter value; zero means that no valid depth return was
available.

To create the labeled contact sheet, install Pillow and run:

```sh
python3 -m pip install Pillow
python3 tools/kinect_capture/compose_capture.py captures/example
```

The generated `kinect_contact_sheet.png` combines RGB, infrared, false-color
depth, and registered color. Local `captures/` output is ignored by Git so live
camera imagery is not committed accidentally.
