# Test plan {#test_plan}

[TOC]

What CI covers automatically, what only a human with a sensor can cover, and
what must be attached to a release. This page is the index; it does not restate
the procedures that already live in @ref quality and
@ref v0_3_upstream_coverage.

## 1. Automated coverage

Every push and pull request runs the `CI` workflow. Full job list and runner
placement in @ref development.

| Dimension | Covered by | Notes |
|---|---|---|
| GCC and Clang, shared and static | `linux-builds` | 4-way matrix, C++17 |
| C++17 conformance | all build jobs | `-DCMAKE_CXX_STANDARD=17` |
| Unit tests | `linux-builds`, `build-test-metal` | `ctest -L unit` |
| Metal/CPU depth parity | `build-test-metal` | self-hosted macOS, real Apple GPU |
| Depth filter backend combinations | `linux-filter-backends` | |
| VAAPI JPEG decoding | `linux-vaapi` | |
| TegraJPEG fallback state machine | `linux-vaapi` | Fake primary; real Tegra hardware is release-only |
| CUDA | *compile only* | No runtime validation in CI |
| ASan + UBSan, TSan | `sanitizers` | |
| Stream-parser fuzzing | `fuzzers` | Bounded smoke run |
| Coverage | `coverage` | LLVM source coverage |
| Lint and static analysis | `format`, `semgrep`, `static-analysis` | |
| Python tooling | `python-quality` | Ruff + pytest |

**What CI does not establish:** OpenGL, OpenCL, and CUDA runtime behavior; USB
transfer behavior; anything requiring a physical sensor. Hosted CI deliberately
does not report GPU or USB runtime success when it only compiled or simulated
those paths.

## 2. Hardware smoke checklist

Run against a connected Kinect v2 after any change to USB handling, the device
lifecycle, or a depth backend. Every command is hardware-free of a viewer, so
this works over SSH.

```sh
# 1. Enumerate, open, stream, close
Protonect -noviewer -frames 30

# 2. Single-stream isolation
Protonect -noviewer -frames 30 -norgb
Protonect -noviewer -frames 30 -nodepth

# 3. Serial selection (multi-device hosts, or to confirm the argument parses)
Protonect -noviewer -frames 30 <serial>

# 4. Every pipeline available on this machine. Substitute the names reported by
#    getAvailablePacketPipelines(); the set below is a macOS example.
for p in cpu metal opengl opencl opencl_kde; do
  LIBFREENECT2_PIPELINE=$p Protonect -noviewer -frames 30
done

# 5. End-to-end capture with registration
mkdir -p /tmp/cap && KinectCapture snapshot /tmp/cap
```

Check for each run:

* The log reports the expected pipeline, and the transfer pool line matches the
  platform default (@ref configuration).
* Capture writes `rgb.ppm`, `depth_mm.pgm`, `infrared.pgm`, `registered.ppm`,
  and `metadata.json`, and `valid_depth_pixels` in the metadata is plausible for
  the scene.
* No errors appear during capture, stop or close. Record teardown failures too;
  see @ref macos_usb_lifecycle for the earlier macOS shutdown workaround.

### Lifecycle

* Repeated open → start → capture → stop → close in one process.
* Immediate restart: stop and start again without closing.
* Unplug and reconnect: the old device pointer must reach a terminal state, and
  capture must resume through a newly opened device object. `KinectReconnect`
  drives this.

The `Kinect hardware soak` workflow automates the repeated-lifecycle and
reconnect cases on a runner with the `kinect` label; see @ref quality and
@ref self_hosted_runner.

### macOS USB lifecycle validation {#macos_usb_lifecycle}

After USB reset, macOS can briefly report an attached service before an
interface becomes claimable. The ownership check retries at 50 ms intervals,
up to ten retries per interface (a nominal 500 ms wait, subject to scheduling).
It still rejects persistent ownership and propagates claim failures without
detaching a driver. The identity of the transient service was not captured.

Close no longer sends the legacy macOS shutdown command or waits four seconds
for the deliberate sensor reboot. After USB transfer cancellation, stop drains
queued and active RGB/depth work; close also drains before clearing listeners,
including cleanup after failed start. The drain applies on all platforms,
while ownership retries and removal of the reboot workaround concern macOS.

#### Hardware-independent regression tests

`usb_control_tests` compiles the real USB control source with only the libusb
ownership query and interface claim functions substituted. It covers transient
and persistent ownership, readiness on the last retry, both interfaces,
unsupported queries and actual claim failures. macOS-only retry expectations
are conditional; other platforms check immediate refusal of ownership.

The processing tests hold the real asynchronous worker at startup, during
processing and during buffer release. They check that drain cannot finish
early, that buffers are returned for rejected packets, and that repeated drain
allows reuse. An empty pipeline is also covered. These tests do not emulate a
complete device lifecycle or establish physical USB behavior.

For a CPU-only test build without a Metal shader compiler:

```sh
cmake -S . -B build-usb-tests -G Ninja \
  -DBUILD_TESTING=ON -DBUILD_EXAMPLES=OFF -DBUILD_OPENNI2_DRIVER=OFF \
  -DENABLE_METAL=OFF -DENABLE_OPENGL=OFF -DENABLE_OPENCL=OFF \
  -DENABLE_CUDA=OFF -DENABLE_VAAPI=OFF -DENABLE_TEGRAJPEG=OFF
cmake --build build-usb-tests --target unit_tests usb_control_tests
ctest --test-dir build-usb-tests -L unit --output-on-failure --timeout 30
```

This excludes GPU validation. Fork pull requests do not run the persistent
self-hosted Metal job; hosted results alone cannot establish Metal behavior.

#### Hardware evidence and remaining scope

Compared with revision `600cef5283121fe37f166423c0e11c7e8a184309`, the fix was
tested on Apple M4 Pro, macOS 26.5.1, Kinect v2 firmware 4.0.3917.0 and libusb
1.0.30, using the Metal pipeline. An Aukey four-port USB-A 3 hub reproduced the
original ownership refusal; a Selore SEUC3303 reproduced the original close
failure. Both negotiated 5 Gb/s and each passed ten full capture/stop/close/
reopen cycles with the final fix. Selore also passed RGB plus depth capture,
active/immediate/open-only close and injected IR submission failure cleanup.

For further hardware checks, repeat these lifecycle cases, require successful
close and immediate reopen, and record the host, hub, link speed, firmware,
pipeline and all errors. Keep lifecycle calls serialized and outside processing
callbacks. A processor that never finishes can still block drain and shutdown.

Older macOS, other firmware, custom asynchronous pipelines, unplug during
streaming and hub operation under concurrent HDMI/Ethernet/peripheral load are
unverified. An Atolla hub enumerated without the sensor, so it supplies no
result for this fix. Depth sequence gaps remain; the parser counter alone
cannot distinguish transport loss from processing backpressure. Partial-claim
rollback and other failed-open resource-accounting issues are outside this fix.

## 3. Release-only evidence

The v0.3.0 gate is specified in @ref v0_3_upstream_coverage. In short, a release
must attach results for:

* Kinect lifecycle soak, including an attended unplug/replug cycle.
* 60-second raw recording and replay validation (@ref recording_replay).
* Intel or AMD VAAPI runtime fallback.
* NVIDIA Jetson TegraJPEG runtime fallback.
* CPU/CUDA recording parity, since CI only compiles CUDA.
* Three-distance plus unseen-holdout depth calibration
  (@ref depth_calibration).

A soak run with `reconnect_cycles: 0` does not satisfy the unplug/replug gate
and must be identified as such.

### TegraJPEG release check

On a Jetson build with TegraJPEG and TurboJPEG enabled, run at least ten minutes
of RGB capture with `LIBFREENECT2_RGB_PROCESSOR=tegrajpeg Protonect -noviewer
-frames 18000`. If TegraJPEG reports an incomplete decoding result, require one
fallback warning, a successful frame for that same packet, and uninterrupted
TurboJPEG frames afterward. If the hardware failure does not occur, record the
run as "not triggered" rather than claiming that runtime recovery was proven.

## 4. Recording results

When attaching evidence to a release or a bug report, record the environment
alongside the outcome — a pass means little without it:

* OS and version, and CPU architecture
* Sensor serial and firmware version (both are logged at start, and written to
  `metadata.json` by `KinectCapture`)
* USB host controller (`lspci -nn`, or `system_profiler SPUSBDataType`)
* GPU and driver version
* Pipeline exercised
* Duration or frame count, and any failures

Example, from a validation run of this checklist:

| Field | Value |
|---|---|
| Host | macOS 15.7.7, Apple M4 Pro (arm64) |
| Sensor | serial `094318334247`, firmware `4.0.3917.0` |
| Link | 5 Gb/s (`Speed: Up to 5 Gb/s`) |
| Pipelines available | `cpu dump metal opengl opencl opencl_kde` |
| Exercised | `metal` (default), `opengl`, `opencl`, `cpu` |
| Result | Capture and registration OK; teardown error recorded before the macOS lifecycle fix |

## Next steps

* @ref quality &mdash; the automated tooling and how to run each profile locally.
* @ref development &mdash; the CI job list and toolchain requirements.
* @ref troubleshooting &mdash; when a smoke-checklist step fails.
