# Benchmarking and performance {#performance}

How to measure this library's per-frame processing cost reproducibly, what the
numbers mean, and where the time actually goes.

This page covers **processing throughput** — how long a depth or color frame
takes to decode once it has arrived. If your problem is frames not arriving at
all, or arriving and being dropped, that is a USB question: see
@ref troubleshooting and @ref linux_usb.

## Building for measurement

Profiling instrumentation is compiled out by default. Turn it on:

```sh
cmake -S . -B build-perf -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_PROFILING=ON
cmake --build build-perf
```

`ENABLE_PROFILING` accumulates a timing sample per frame per stage in memory for
the process's lifetime, so it is memory-hungry on long runs and should never be
enabled in a shipping build. It is listed among the build options in the
[README](https://github.com/hbmartin/libfreenect2-metal#build-options).

## Running a measurement

`Protonect` is the benchmark harness. The flags that matter:

| Flag | Effect |
|---|---|
| `-noviewer` | Skip the GL viewer, so you measure decoding rather than display |
| `-frames <n>` | Stop after `n` frames |
| `-norgb` / `-nodepth` | Isolate one stream |
| `-gpu=<id>` | Select a GPU when more than one is present |
| `LOGFILE=/dev/null` | Send library logging away from the console |

A single-pipeline run:

```sh
LOGFILE=/dev/null ./build-perf/bin/Protonect -noviewer -frames 3000 metal
```

Repeat per pipeline you care about: `cpu`, `gl`, `cl`, `clkde`, `cuda`,
`cudakde`, `metal`. On Linux, `LIBVA_DRIVER_NAME=none` forces the color path
back to TurboJPEG so you can separate the depth pipeline's cost from the JPEG
decoder's — otherwise VAAPI may be selected automatically and the two changes
confound each other. See @ref configuration for decoder selection.

Statistics are printed at process exit, one line per timed stage:

```
<stage> <min> <5%> <median> <95%> <max> mean=<mean> std=<std> n=<samples>
```

All values are milliseconds per frame. **Report the median**, not the mean —
the distributions have long right tails from scheduling jitter, and a single
stall skews the mean substantially.

## What to record

A benchmark result is meaningless without its configuration. Capture:

1. CPU and GPU model.
2. OS and version; on Linux, the kernel version.
3. Compiler version, and the API version of whichever backend you used
   (OpenGL / OpenCL / CUDA / Metal).
4. Date.
5. The exact `Protonect` invocation.

On Linux, `top -d1` in thread view (`H`), tree view (`V`), Irix mode (`I`) gives
per-thread CPU usage, which is worth reporting alongside the per-frame times —
it distinguishes "the GPU pipeline is fast" from "the GPU pipeline is fast and
also leaves the CPU free."

## Where the time goes

Two costs dominate, and they are independent:

* **Depth processing** — the phase-unwrapping and filtering math over ten
  subframes. This is what the `cpu` / `gl` / `cl` / `cuda` / `metal` choice
  selects, and the spread between them is enormous: two orders of magnitude
  between the CPU processor and a discrete GPU.
* **Color decoding** — a 1920×1080 JPEG per frame. This is what the RGB decoder
  choice selects. A hardware decoder cuts it roughly threefold versus TurboJPEG.

Registration and USB handling are comparatively minor but not free; in upstream's
measurements registration alone accounted for up to 20% of a core.

The practical consequence: if your depth pipeline is already on a GPU, the JPEG
decoder becomes the bottleneck, and vice versa. Measure both before optimizing
either.

## Hardware JPEG decoding by platform

Which platforms can offload color decoding, and why the others cannot:

| Platform | Support | Notes |
|---|---|---|
| Intel on Linux (VA-API) | Good | The `vaapi` decoder; Ivy Bridge or newer |
| Apple (VideoToolbox) | Partial | Present, but not hardware-accelerated in practice, and off by default on Apple Silicon because it crashes there. TurboJPEG is used instead. |
| Tegra | Yes | The only NVIDIA product line with a hardware JPEG decoder |
| NVIDIA desktop (VDPAU) | No | VDPAU does not support JPEG at all |
| AMD | No | Decoding is possible via OpenCL, but it would compete with depth processing for the same GPU |
| Intel on Windows (Media SDK) | Not implemented | Technically possible through the Media Foundation MJPEG transform |

Everything else falls back to TurboJPEG on the CPU, which is fast enough to keep
up with 30 Hz on any modern desktop core.

## Historical results

The table below is upstream's February 2016 benchmark data, preserved from the
[libfreenect2 wiki](https://github.com/OpenKinect/libfreenect2/wiki/Performance).
Median per-frame times in milliseconds.

| Configuration | Hardware | Depth | RGB |
|---|---|---|---|
| CPU / TurboJPEG | i7-4770K | 201.4 | 13.3 |
| CPU / VAAPI | i7-4770K | 196.8 | 4.6 |
| OpenGL / TurboJPEG | i7-4770K + GTX 980 Ti | 3.7 | 13.5 |
| OpenCL / TurboJPEG | i7-4770K + GTX 980 Ti | 1.1 | 13.8 |
| CUDA / TurboJPEG | i7-4770K + GTX 980 Ti | 0.86 | 13.2 |
| CUDA / VAAPI | i7-4770K + GTX 980 Ti | 0.86 | 4.5 |
| OpenGL / TurboJPEG | i7-4600U + HD 4400 | 15.9 | 18.9 |
| OpenCL / VAAPI | i7-4600U + HD 4400 | 13.2 | 5.0 |
| CUDA / TegraJPEG | Cortex-A15 + Tegra K1 | 10.7 | 11.9 |

**Treat these as shape, not as targets.** They were taken on Ubuntu 14.04 with
CUDA 6.5/7.5 and GCC 4.8 on hardware that is now a decade old, against an older
version of the library. What survives is the ordering — CUDA ≲ OpenCL < OpenGL ≪
CPU for depth, and hardware JPEG ≈ 3× TurboJPEG for color — and the observation
that 30 Hz (33.3 ms/frame) is comfortably met by every GPU configuration and not
met by the CPU processor on the hardware of that era.

This fork's Metal pipeline is not represented; it postdates the data set. CI
runs a Metal-versus-CPU **parity** test on real hardware, which checks
correctness rather than speed.

## Contributing measurements

Numbers from current hardware are welcome, particularly Apple Silicon with
Metal. Open an issue with the configuration list above, the raw stage lines, and
the `Protonect` invocation.

## Next steps

* @ref configuration &mdash; pipeline and RGB decoder selection.
* @ref linux_usb &mdash; when the limit is transport rather than compute.
* @ref quality &mdash; the correctness checks that run alongside.
