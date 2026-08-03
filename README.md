# libfreenect2-metal

[![CI](https://github.com/hbmartin/libfreenect2-metal/actions/workflows/ci.yml/badge.svg)](https://github.com/hbmartin/libfreenect2-metal/actions/workflows/ci.yml)
[![Documentation](https://img.shields.io/badge/docs-online-blue.svg)](https://hbmartin.github.io/libfreenect2-metal/)
[![License](https://img.shields.io/badge/license-Apache--2.0%20OR%20GPL--2.0-blue.svg)](#license)
<!-- Keep the version badge and the Version section in sync with PROJECT_VERSION in CMakeLists.txt. -->
[![Version](https://img.shields.io/badge/version-0.4.0-blue.svg)](#version)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](#requirements)
[![Platforms](https://img.shields.io/badge/platforms-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey.svg)](#installation)

Open source cross-platform driver for the **Kinect for Windows v2** (K4W2)
sensor. It streams color, infrared, and depth over USB 3.0 and registers color
to depth so you can build point clouds — on Linux, Windows, and macOS, including
Apple Silicon with a Metal GPU pipeline.

📖 **[Full documentation and API reference →](https://hbmartin.github.io/libfreenect2-metal/)**

<!-- TODO: demo GIF -->

> This is [hbmartin/libfreenect2-metal](https://github.com/hbmartin/libfreenect2-metal),
> an actively maintained fork of
> [OpenKinect/libfreenect2](https://github.com/OpenKinect/libfreenect2).
> Clone **this** repository — upstream has no Metal pipeline and none of the 0.4
> APIs described below. See [What's different in this fork](#whats-different-in-this-fork).

Note: this driver does nothing for Kinect for Windows v1 or Kinect for Xbox 360
sensors. Use [libfreenect](https://github.com/OpenKinect/libfreenect) for those.
Not sure which one you have, or why v1 advice keeps not working?
See [Kinect v1 versus Kinect v2](doc/kinect_v1_vs_v2.md).

## Table of Contents

* [What's different in this fork](#whats-different-in-this-fork)
* [Quickstart](#quickstart)
* [Requirements](#requirements)
* [Installation](#installation)
* [Build options](#build-options)
* [Depth pipelines](#depth-pipelines)
* [Example programs](#example-programs)
* [Documentation](#documentation)
* [Troubleshooting](#troubleshooting)
* [Development and contributing](#development-and-contributing)
* [License](#license)
* [Credits](#credits)

## What's different in this fork

* **Metal depth pipeline** for Apple Silicon, enabled by default on Apple
  platforms and preferred over the deprecated OpenGL path. CI runs a
  Metal-versus-CPU parity test on real hardware.
* **The 0.4 API**: conventional calibration profiles and projective
  registration, runtime health snapshots, typed caller-owned frames, and
  recording manifest v2. See [Migrating to libfreenect2 0.4](doc/v0.4_migration.md).
* **`libfreenect2/vision.h`** — a C++17 interface for validated caller-buffer
  color conversion, forward and reverse registration maps, coherent depth
  selection, and batched metric XYZ lifting.
* **Recording and replay**: capture raw streams to disk and replay them through
  any pipeline, including running multiple Kinects. See
  [Recording, replay, and multiple Kinects](doc/recording_replay.md).
* **Per-device depth calibration**: fit, validate, and apply a linear depth
  correction profile. See
  [Fitting and applying per-device depth correction](doc/depth_calibration.md).
* **Self-contained CUDA builds** that need only the CUDA Toolkit — NVIDIA's
  samples and the former `helper_math.h` are not required, and CMake does not
  search sample paths.
* **A real quality gate**: unit tests, ASan/UBSan/TSan profiles, libFuzzer
  targets over the stream parsers, LLVM coverage, clang-tidy, and Semgrep, all
  wired into CI. See [Quality checks](doc/quality.md).
* **Python bindings** via
  [pylibfreenect3](https://github.com/hbmartin/pylibfreenect3), which binds the
  0.3 device, recording, alignment, registration, and vision APIs without a
  ctypes bridge.
* **Docs**: a published API reference plus task-oriented guides, listed under
  [Documentation](#documentation).

Missing features: firmware updates (see
[issue #460](https://github.com/OpenKinect/libfreenect2/issues/460) for WiP) and
calibrated directional audio. Native Kinect SDK-style body/skeleton tracking is
also out of scope; the supported
[Python pose-estimation workflow](doc/python.md#pose-estimation-workflow) uses
MediaPipe plus registered depth and produces estimates rather than
sensor-provided joints.

## Quickstart

Install for your platform ([macOS](doc/install_macos.md),
[Linux](doc/install_linux.md), [Windows](doc/install_windows.md)), then open a
device and pull registered frames:

```cpp
#include <libfreenect2/libfreenect2.hpp>
#include <libfreenect2/frame_listener_impl.h>
#include <libfreenect2/registration.h>

int main()
{
  libfreenect2::Freenect2 freenect2;
  if (freenect2.enumerateDevices() == 0)
    return 1;

  // Picks the best available pipeline: metal > opengl > cuda > opencl > cpu.
  libfreenect2::Freenect2Device *dev =
      freenect2.openDevice(freenect2.getDefaultDeviceSerialNumber());
  if (dev == 0)
    return 1;

  libfreenect2::SyncMultiFrameListener listener(
      libfreenect2::Frame::Color | libfreenect2::Frame::Ir | libfreenect2::Frame::Depth);
  dev->setColorFrameListener(&listener);
  dev->setIrAndDepthFrameListener(&listener);
  if (!dev->start())
    return 1;

  libfreenect2::Registration registration(dev->getIrCameraParams(),
                                          dev->getColorCameraParams());
  libfreenect2::Frame undistorted(512, 424, 4, nullptr,
                                  libfreenect2::Frame::Float);
  libfreenect2::Frame registered(512, 424, 4, nullptr,
                                 libfreenect2::Frame::BGRX);

  libfreenect2::FrameMap frames;
  for (int i = 0; i < 100; ++i)
  {
    if (!listener.waitForNewFrame(frames, 10 * 1000))  // 10 s timeout
      break;

    libfreenect2::Frame *rgb = frames[libfreenect2::Frame::Color];
    libfreenect2::Frame *depth = frames[libfreenect2::Frame::Depth];

    // `registered` now holds color aligned to the 512x424 depth image.
    registration.apply(rgb, depth, &undistorted, &registered);

    listener.release(frames);
  }

  dev->stop();
  dev->close();
  return 0;
}
```

Consume the installed library from CMake:

```cmake
find_package(freenect2 REQUIRED)

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE freenect2::freenect2)
```

If you installed to a non-standard prefix, point CMake at it:

```sh
cmake -Dfreenect2_DIR=$HOME/freenect2/lib/cmake/freenect2 ..
```

More: the [API walkthrough](https://hbmartin.github.io/libfreenect2-metal/),
the [registration recipes](doc/registration.md), and
[`examples/`](#example-programs).

## Requirements

### Hardware

* A **USB 3.0 controller**, one per sensor. USB 2 is not supported. Intel and
  NEC host controllers are widely reported to work; ASMedia controllers are
  widely reported not to.
* Virtual machines likely do not work, because USB 3.0 isochronous transfer is
  delicate.
* The sensor's dedicated AC adapter — the USB connection alone will not power it.

Why one controller per sensor, and how to confirm the link negotiated
SuperSpeed: [USB bandwidth and transfer tuning](doc/linux_usb.md).

### Platform support

| Tier | Platforms |
|---|---|
| **Tested** | Ubuntu 22.04/24.04 (GCC/Clang × shared/static, sanitizers, fuzzers, coverage); macOS on Apple Silicon (Metal, C++17, Metal/CPU parity on real hardware) |
| **Expected to work** | Ubuntu 22.04 LTS and newer, Debian 12 and newer, other current distributions with libusb ≥ 1.0.20 and kernel ≥ 5.15; macOS on Intel; Windows 10 and newer |
| **Unsupported** | Ubuntu 20.04 and older, Debian 11 and older, Windows 8 and older, any USB 2 host, virtual machines, Jetson TK1/TX1 |

Older platforms are not blocked by the build system, but they are untested and
some optional-backend packages no longer exist for them.

### Toolchain

CMake 3.16 or newer, and a C++17 compiler. CI builds at C++17 with GCC, Clang,
and AppleClang.

### Optional features

| Feature | Requirement |
|---|---|
| Metal depth processing | macOS (Apple platforms) |
| OpenGL depth processing | OpenGL 3.1. OpenGL ES is not supported. |
| OpenCL depth processing | OpenCL 1.1 |
| CUDA depth processing | CUDA Toolkit (CUDA 12.3 is covered by compile-only CI) |
| VAAPI JPEG decoding | Intel Ivy Bridge or newer, Linux only |
| VideoToolbox JPEG decoding | macOS only (off by default on Apple Silicon) |
| OpenNI2 integration | OpenNI2 2.2.0.33 |
| Offline conventional calibration | OpenCV 4.5 or newer; opt in with `BUILD_CALIBRATION_TOOLS=ON` |

## Installation

Full, platform-specific instructions live in `doc/`:

* **[macOS install guide](doc/install_macos.md)** — including Apple Silicon and
  architecture-mismatch fixes
* **[Linux install guide](doc/install_linux.md)** — including udev rules, all
  optional backends, and legacy distributions
* **[Windows install guide](doc/install_windows.md)** — including UsbDk/libusbK
  driver setup and vcpkg

Condensed versions:

<details>
<summary><b>macOS</b></summary>

```sh
brew install cmake pkg-config ninja libusb glfw3 jpeg-turbo

git clone https://github.com/hbmartin/libfreenect2-metal.git
cd libfreenect2-metal

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build

./build/bin/Protonect
```

On Apple Silicon, run the build from a **native arm64 terminal** — `arch` must
print `arm64`. A shell or IDE under Rosetta produces x86_64 builds that cannot
link Homebrew's arm64 libraries in `/opt/homebrew`. CMake detects this at
configure time and stops with instructions. Full detail:
[macOS install guide](doc/install_macos.md).

</details>

<details>
<summary><b>Linux</b></summary>

```sh
sudo apt-get install build-essential cmake pkg-config ninja-build \
  libusb-1.0-0-dev libturbojpeg0-dev libglfw3-dev

git clone https://github.com/hbmartin/libfreenect2-metal.git
cd libfreenect2-metal

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=$HOME/freenect2
cmake --build build
cmake --install build

# Device access — required, otherwise the sensor is root-only
sudo cp platform/linux/udev/90-kinect2.rules /etc/udev/rules.d/
# then unplug and replug the Kinect

./build/bin/Protonect
```

Minimum supported release is Ubuntu 22.04 LTS. Optional backends (OpenCL, CUDA,
VAAPI, OpenNI2), support tiers, and multi-sensor setup:
[Linux install guide](doc/install_linux.md).

</details>

<details>
<summary><b>Windows / Visual Studio</b></summary>

1. Install a USB driver — **either** [UsbDk](https://github.com/daynix/UsbDk/releases)
   (recommended) **or** libusbK via [Zadig](http://zadig.akeo.ie/). Not both.
2. Install dependencies into `depends/`: [libusb](https://github.com/libusb/libusb/releases)
   as `depends/libusb`, [TurboJPEG](http://sourceforge.net/projects/libjpeg-turbo/files)
   to `c:\libjpeg-turbo64`, and [GLFW](http://www.glfw.org/download.html) as
   `depends/glfw`.
3. Build:
   ```
   mkdir build && cd build
   cmake .. -G "Visual Studio 16 2019"
   cmake --build . --config RelWithDebInfo --target install
   ```
4. Run `.\install\bin\Protonect.exe`.

Exact driver steps, uninstall instructions, and optional backends:
[Windows install guide](doc/install_windows.md).

</details>

<details>
<summary><b>Windows / vcpkg</b></summary>

```
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
./vcpkg integrate install
vcpkg install libfreenect2
```

Note that the vcpkg port tracks **upstream** `OpenKinect/libfreenect2`, not this
fork, so it has no Metal pipeline or 0.4 APIs.

</details>

<details>
<summary><b>CUDA (self-contained)</b></summary>

The `cuda` and `cuda_kde` pipelines depend only on headers and libraries from
the CUDA Toolkit. NVIDIA's CUDA samples and their former `helper_math.h` header
are not required, and CMake does not search sample installation paths.

On a machine with an NVIDIA GPU:

```sh
cmake -S . -B build-cuda -DENABLE_CUDA=ON -DCMAKE_CUDA_ARCHITECTURES=native
cmake --build build-cuda --target freenect2
```

For a GPU-less build host or container, specify the architecture explicitly, for
example `-DCMAKE_CUDA_ARCHITECTURES=75`. CMake 3.23 and newer otherwise defaults
to `all-major`, so configuration does not need to query a local GPU.

Hosted CI compiles both pipelines with CUDA 12.3 but does not claim runtime
validation; compare CUDA and CPU output on real hardware before a release.

</details>

## Build options

Pass these to CMake as `-DOPTION=VALUE`.

| Option | Default | Effect |
|---|---|---|
| `BUILD_SHARED_LIBS` | `ON` | Build shared (`ON`) or static (`OFF`) libraries |
| `BUILD_EXAMPLES` | `ON` | Build the [example programs](#example-programs) |
| `BUILD_OPENNI2_DRIVER` | `ON` | Build the OpenNI2 driver |
| `BUILD_TESTING` | `OFF` | Build the unit test suite |
| `BUILD_CALIBRATION_TOOLS` | `OFF` | Build the headless OpenCV calibration/YAML conversion tool |
| `ENABLE_METAL` | `ON` on Apple, else `OFF` | Metal GPU depth processing |
| `ENABLE_OPENGL` | `ON` | OpenGL depth processing (needs OpenGL 3.1) |
| `ENABLE_OPENCL` | `ON` | OpenCL depth processing |
| `ENABLE_CUDA` | `ON` | CUDA depth processing |
| `ENABLE_VAAPI` | `ON` | VA-API JPEG decoding (Intel, Linux) |
| `ENABLE_VIDEOTOOLBOX` | `OFF` on Apple Silicon, `ON` on Intel Mac | VideoToolbox RGB decoder. It crashes on M1 and later, hence the split default; TurboJPEG is used instead. |
| `ENABLE_TEGRAJPEG` | `ON` | Tegra hardware JPEG support |
| `ENABLE_PROFILING` | `OFF` | Collect profiling stats (memory consuming) |
| `BUILD_STREAMER_RECORDER` | `OFF` | Build the `streamer_recorder` tool |
| `ENABLE_SANITIZERS` | `OFF` | ASan + UBSan on first-party targets |

An `ENABLE_*` backend is silently skipped if its dependencies are not found; the
CMake configure summary reports what was actually enabled. Development-only
flags — warnings-as-errors, individual sanitizers, coverage, fuzzing, stdlib
hardening — are documented in
[Development and contributing](doc/development.md#quality-and-instrumentation-options).

## Depth pipelines

| Name | Platform | Requires | Notes |
|---|---|---|---|
| `cpu` | all | — | Always available; terminates the fallback chain |
| `metal` | Apple | `ENABLE_METAL` | Preferred on Apple Silicon; OpenGL is deprecated by Apple |
| `opengl` | all | OpenGL 3.1 | OpenGL ES is not supported |
| `opencl` | all | OpenCL 1.1 | |
| `opencl_kde` | all | OpenCL 1.1 | KDE depth unwrapping |
| `cuda` | NVIDIA | CUDA Toolkit | |
| `cuda_kde` | NVIDIA | CUDA Toolkit | KDE depth unwrapping |
| `dump` | all | — | Dumps raw frames instead of decoding depth |

Select one at runtime:

```sh
LIBFREENECT2_PIPELINE=metal ./Protonect
```

The older `gl` and `cl` spellings are accepted **only** as aliases through
`LIBFREENECT2_PIPELINE`; everywhere else use the canonical names above. If the
requested pipeline is unavailable, libfreenect2 logs a warning and falls through
the chain `metal → opengl → cuda → opencl → cpu`, probing each for a usable
runtime device. The opened device reports the pipeline it actually consumed.

`Protonect` additionally accepts a pipeline as a positional argument using its
own short vocabulary: `cpu`, `gl`, `cl`, `clkde`, `cuda`, `cudakde`, `metal`.
Unlike the environment preference, a positional GPU selection is strict:
Protonect exits before opening a sensor when that backend is not compiled or
has no usable runtime device.

To discover pipelines programmatically, `getCompiledPacketPipelines()` returns
the canonical names built into the library and `getAvailablePacketPipelines()`
returns those usable on the current machine. Availability probing constructs
each pipeline and can initialize GPU runtimes, so keep it off latency-sensitive
paths.

Other environment variables — logging level and USB buffer tuning — are in the
[runtime configuration reference](doc/configuration.md).

## Example programs

Built when `BUILD_EXAMPLES=ON` (the default), into `build/bin`.

| Program | What it does |
|---|---|
| `Protonect` | The reference viewer and smoke test. Displays color, IR, depth, and registered output. `Protonect [-gpu=<id>] [gl\|cl\|clkde\|cuda\|cudakde\|metal\|cpu] [<serial>]` |
| `KinectCapture` | Writes frames to disk: continuous capture, timestamp-paired `snapshot`, or raw stream `record`. Supports canonical calibration and depth-correction profiles. |
| `KinectCameraCalibration` | Optional headless, recording-driven conventional camera calibration and legacy YAML conversion tool. See [calibration profiles](doc/calibration_profiles.md). |
| `KinectDepthCalibration` | Fits a per-device linear depth correction profile from live or recorded data over a known-distance ROI. See [depth calibration](doc/depth_calibration.md). |
| `KinectReconnect` | Exercises disconnect and recovery handling. `KinectReconnect [SERIAL]` |

`examples/CMakeLists.txt` doubles as a standalone build system for an
out-of-tree application linking an installed libfreenect2.

## Documentation

The full site, including the API reference and every guide below, is published
at **<https://hbmartin.github.io/libfreenect2-metal/>**.

**Upgrade**
* [Migrating to libfreenect2 0.4](https://hbmartin.github.io/libfreenect2-metal/v0_4_migration.html)
* [Migrating to libfreenect2 0.3](https://hbmartin.github.io/libfreenect2-metal/v0_3_migration.html)
* [v0.3 upstream issue coverage](https://hbmartin.github.io/libfreenect2-metal/v0_3_upstream_coverage.html)

**Install and fix**
* [macOS](https://hbmartin.github.io/libfreenect2-metal/install_macos.html) · [Linux](https://hbmartin.github.io/libfreenect2-metal/install_linux.html) · [Windows](https://hbmartin.github.io/libfreenect2-metal/install_windows.html)
* [Troubleshooting](https://hbmartin.github.io/libfreenect2-metal/troubleshooting.html)
* [USB bandwidth and transfer tuning](https://hbmartin.github.io/libfreenect2-metal/linux_usb.html)
* [FAQ](https://hbmartin.github.io/libfreenect2-metal/faq.html)
* [Runtime configuration reference](https://hbmartin.github.io/libfreenect2-metal/configuration.html)
* [Kinect v1 versus Kinect v2](https://hbmartin.github.io/libfreenect2-metal/kinect_v1_vs_v2.html)

**Work with the data**
* [Conventional camera calibration profiles](https://hbmartin.github.io/libfreenect2-metal/calibration_profiles.html)
* [Registration and coordinate mapping recipes](https://hbmartin.github.io/libfreenect2-metal/registration.html)
* [Depth accuracy and calibration](https://hbmartin.github.io/libfreenect2-metal/depth_accuracy.html)
* [Fitting and applying per-device depth correction](https://hbmartin.github.io/libfreenect2-metal/depth_calibration.html)
* [Frame timing and software pairing](https://hbmartin.github.io/libfreenect2-metal/frame_timing.html)

**Integrate**
* [Recording, replay, and multiple Kinects](https://hbmartin.github.io/libfreenect2-metal/recording_replay.html)
* [Using the Kinect v2 as a webcam](https://hbmartin.github.io/libfreenect2-metal/webcam.html)
* [Using libfreenect2 from Python](https://hbmartin.github.io/libfreenect2-metal/python.html)

**Go deeper**
* [Kinect v2 USB protocol](https://hbmartin.github.io/libfreenect2-metal/protocol.html)
* [Benchmarking and performance](https://hbmartin.github.io/libfreenect2-metal/performance.html)

**Maintain**
* [Development and contributing](https://hbmartin.github.io/libfreenect2-metal/development.html)
* [Quality checks, sanitizers, fuzzing, and coverage](https://hbmartin.github.io/libfreenect2-metal/quality.html)
* [Test plan](https://hbmartin.github.io/libfreenect2-metal/test_plan.html)
* [Self-hosted runner setup](https://hbmartin.github.io/libfreenect2-metal/self_hosted_runner.html)

## Troubleshooting

See the **[troubleshooting guide](doc/troubleshooting.md)** first; the upstream
[troubleshooting wiki](https://github.com/OpenKinect/libfreenect2/wiki/Troubleshooting)
still holds useful hardware-specific notes.

Report bugs at <https://github.com/hbmartin/libfreenect2-metal/issues>. For USB
issues, attach the output of the program run with `LIBUSB_DEBUG=3`, the relevant
`dmesg` log, and hardware information from `lspci` and `lsusb -t`.

## Development and contributing

Issues and pull requests are welcome at
<https://github.com/hbmartin/libfreenect2-metal>.

```sh
cmake -S . -B build-dev -G Ninja -DBUILD_TESTING=ON -DENABLE_WARNINGS_AS_ERRORS=ON
cmake --build build-dev
ctest --test-dir build-dev --output-on-failure
```

The repository's Python tooling requires **Python 3.12 or newer** and `uv`; it
is not needed to build or use the library. Toolchain requirements, the CI
matrix, sanitizer and fuzzing profiles, formatting rules, and the docs build are
all covered in **[Development and contributing](doc/development.md)**.

## Version

The current version is **0.4.0** (`PROJECT_VERSION` in `CMakeLists.txt`). The
library also reports its version, API version, and build revision at runtime;
see [Migrating to libfreenect2 0.4](doc/v0.4_migration.md).

## License

libfreenect2 is available under **your choice of** either:

* the Apache License, Version 2.0 (`Apache-2.0`), or
* the GNU General Public License, Version 2.0 only (`GPL-2.0-only`).

```
SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-only
```

Full texts are in [`APACHE20`](APACHE20) and [`GPL2`](GPL2); see
[`LICENSE`](LICENSE) for the summary. Individual files may carry additional
attribution or redistribution notices that must be preserved. Third-party
components remain under their own licenses, with notices alongside those
components and in `depends/LICENSES.txt`.

## Credits

This fork is maintained by [Harold Martin](https://github.com/hbmartin).

Upstream libfreenect2 maintainers:

* Joshua Blake <joshblake@gmail.com>
* Florian Echtler
* Christian Kerl
* Lingzhu Xiang (development/master branch)

Contributor attributions are collected in [`CONTRIB`](CONTRIB).

If you use the KDE depth unwrapping algorithm implemented in this library,
please cite the ECCV 2016
[paper](http://users.isy.liu.se/cvl/perfo/abstracts/jaremo16.html).

The [libfreenect2 wiki](https://github.com/OpenKinect/libfreenect2/wiki) and the
[mailing list](https://groups.google.com/forum/#!forum/openkinect) carry
background on the K4W2 USB protocol; what remained useful from the wiki has been
absorbed into [`doc/`](#documentation) — see
[the protocol reference](doc/protocol.md), [performance](doc/performance.md),
[USB notes](doc/linux_usb.md), and [troubleshooting](doc/troubleshooting.md).
(The former openkinect.org domain, which hosted the **Kinect v1** wiki, has
lapsed and now serves unrelated ads — do not use it.)
