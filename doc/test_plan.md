# Test plan {#test_plan}

What CI covers automatically, what only a human with a sensor can cover, and
what must be attached to a release. This page is the index; it does not restate
the procedures that already live in @ref quality and
@ref v0_3_upstream_coverage.

## 1. Automated coverage

Every push and pull request runs the `CI` workflow. Full job list and runner
placement in @ref development.

| Dimension | Covered by | Notes |
|---|---|---|
| GCC and Clang, shared and static | `linux-builds` | 4-way matrix, C++11 |
| C++11 conformance | all build jobs | `-DCMAKE_CXX_STANDARD=11` |
| Unit tests | `linux-builds`, `build-test-metal` | `ctest -L unit` |
| Metal/CPU depth parity | `build-test-metal` | self-hosted macOS, real Apple GPU |
| Depth filter backend combinations | `linux-filter-backends` | |
| VAAPI JPEG decoding | `linux-vaapi` | |
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
* No errors appear *before* `stopped`. See
  @ref troubleshooting for messages that are known-harmless at teardown.

### Lifecycle

* Repeated open → start → capture → stop → close in one process.
* Immediate restart: stop and start again without closing.
* Unplug and reconnect: the old device pointer must reach a terminal state, and
  capture must resume through a newly opened device object. `KinectReconnect`
  drives this.

The `Kinect hardware soak` workflow automates the repeated-lifecycle and
reconnect cases on a runner with the `kinect` label; see @ref quality and
@ref self_hosted_runner.

## 3. Release-only evidence

The v0.3.0 gate is specified in @ref v0_3_upstream_coverage. In short, a release
must attach results for:

* Kinect lifecycle soak, including an attended unplug/replug cycle.
* 60-second raw recording and replay validation (@ref recording_replay).
* Intel or AMD VAAPI runtime fallback.
* CPU/CUDA recording parity, since CI only compiles CUDA.
* Three-distance plus unseen-holdout depth calibration
  (@ref depth_calibration).

A soak run with `reconnect_cycles: 0` does not satisfy the unplug/replug gate
and must be identified as such.

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
| Result | Capture and registration OK; benign teardown error only |

## Next steps

* @ref quality &mdash; the automated tooling and how to run each profile locally.
* @ref development &mdash; the CI job list and toolchain requirements.
* @ref troubleshooting &mdash; when a smoke-checklist step fails.
