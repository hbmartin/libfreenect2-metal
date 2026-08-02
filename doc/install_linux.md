# Installing on Linux {#install_linux}

This is the reference install path for Linux. For a condensed version see the
[README](https://github.com/hbmartin/libfreenect2-metal#readme).

**Minimum supported release: Ubuntu 22.04 LTS (Jammy Jellyfish)** or the
equivalent Debian. Package names below are for Jammy and newer; adapt them for
other distributions.

## Support tiers

| Tier | Platforms |
|---|---|
| **Tested** | Ubuntu 24.04 (CI: GCC and Clang, shared and static, filter backends, VAAPI, sanitizers, fuzzers, coverage) |
| **Expected to work** | Ubuntu 22.04 LTS and newer; Debian 12 and newer; other current distributions with libusb ≥ 1.0.20 and kernel ≥ 5.15 |
| **Unsupported** | Ubuntu 20.04 and older, Debian 11 and older, any USB 2 host, virtual machines, 32-bit ARM Jetson TK1/TX1 |

Older releases are not blocked by the build system, but they are not tested and
their optional-backend packages (notably Beignet, which was removed from Ubuntu
after 18.04) no longer exist.

## Requirements

* A USB 3.0 controller. USB 2 is not supported. Intel and NEC host controllers
  are widely reported to work; ASMedia controllers are widely reported not to.
* Kernel 5.15 or newer (the Ubuntu 22.04 default).
* libusb 1.0.20 or newer (Ubuntu 22.04 ships 1.0.25).

Virtual machines usually do not work, because USB 3.0 isochronous transfer is
delicate.

## Build tools

```sh
sudo apt-get install build-essential cmake pkg-config ninja-build
```

## Source

```sh
git clone https://github.com/hbmartin/libfreenect2-metal.git
cd libfreenect2-metal
```

Cloning `OpenKinect/libfreenect2` instead gets you upstream, which has none of
the 0.3 APIs described in the guides.

## Required dependencies

```sh
sudo apt-get install libusb-1.0-0-dev libturbojpeg0-dev
```

## Optional dependencies

### OpenGL (example viewer and the `opengl` pipeline)

```sh
sudo apt-get install libglfw3-dev
```

OpenGL 3.1 is required. On platforms that lack it, configure with
`-DENABLE_OPENGL=OFF`. OpenGL ES is not supported.

### OpenCL

Install the ICD loader and development headers, then a vendor runtime:

```sh
sudo apt-get install ocl-icd-opencl-dev clinfo
```

| Vendor | Package |
|---|---|
| Intel (Gen8+) | `intel-opencl-icd` — the NEO compute runtime. Beignet, the pre-18.04 Intel stack, no longer exists in Ubuntu. |
| AMD | `mesa-opencl-icd` for the open stack, or AMD's ROCm packages. The Catalyst driver referenced by older guides is discontinued. |
| NVIDIA | Provided by the proprietary driver package |

Verify the stack independently of libfreenect2 before suspecting the driver:

```sh
clinfo | head
```

If `clinfo` reports no platforms, libfreenect2 will skip the OpenCL pipelines —
that is a driver problem, not a libfreenect2 one.

### CUDA (NVIDIA only)

Follow NVIDIA's toolkit and driver instructions for your distribution. The CUDA
**samples package is not required** — the `cuda` and `cuda_kde` pipelines depend
only on CUDA Toolkit headers and libraries, and CMake does not search sample
installation paths.

On a machine with an NVIDIA GPU:

```sh
cmake -S . -B build-cuda -DENABLE_CUDA=ON -DCMAKE_CUDA_ARCHITECTURES=native
cmake --build build-cuda --target freenect2
```

On a GPU-less build host or container, specify the target architecture
explicitly, for example `-DCMAKE_CUDA_ARCHITECTURES=75`. CMake 3.23 and newer
otherwise defaults to `all-major`, so configuration does not need to query a
local GPU.

Hosted CI compiles both CUDA pipelines with CUDA 12.3 but does not claim runtime
validation. Compare CUDA and CPU output on real hardware before a release; see
@ref test_plan.

On a system with both NVIDIA and Intel GPUs, keeping the Intel GPU for the
desktop avoids contention with the depth pipeline.

### VAAPI JPEG decoding (Intel only)

Requires Ivy Bridge or newer.

```sh
sudo apt-get install libva-dev libjpeg-dev
```

If autodetection picks the wrong render node, select one explicitly with
`LIBFREENECT2_VAAPI_DEVICE=/dev/dri/renderD128`.

### OpenNI2

```sh
sudo apt-get install libopenni2-dev
```

Requires OpenNI2 2.2.0.33.

## Build and install

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=$HOME/freenect2
cmake --build build
cmake --install build
```

To let a CMake-based third-party application find the installation:

```sh
cmake -Dfreenect2_DIR=$HOME/freenect2/lib/cmake/freenect2 ...
```

## Device access (udev rules)

Without this, the device is only accessible as root.

```sh
sudo cp platform/linux/udev/90-kinect2.rules /etc/udev/rules.d/
```

Then unplug and replug the Kinect. The rule sets `MODE="0666"` on vendor `045e`,
products `02c4`, `02d8`, and `02d9`. To confirm it applied, check the node mode
rather than running the viewer under `sudo` — see @ref troubleshooting.

## Run

```sh
./build/bin/Protonect
./build/bin/Protonect -noviewer -frames 30   # headless
```

## Multiple Kinects

Up to 5 devices have been reported working on a high-end PC using multiple
separate PCI Express USB3 expansion cards with NEC controller chips.

* **One sensor per USB3 host controller.** This is the binding constraint.
* Do not plug an expansion card into a PCIe x1 slot — one lane does not have
  enough bandwidth. Use x8 or x16.
* Raise the usbfs memory limit, further for each additional device:
  ```sh
  cat /sys/module/usbcore/parameters/usbfs_memory_mb
  echo 1000 | sudo tee /sys/module/usbcore/parameters/usbfs_memory_mb
  ```
  See @ref linux_usb to persist this across reboots.

See @ref recording_replay for opening and pairing several devices in software,
and @ref linux_usb for why the one-per-controller rule exists.

## Testing OpenNI2 (optional)

```sh
sudo apt-get install openni2-utils
sudo cmake --build build --target install-openni2
NiViewer2
```

Set `LIBFREENECT2_PIPELINE` to select a pipeline, for example
`LIBFREENECT2_PIPELINE=opencl NiViewer2`.

## Next steps

* @ref troubleshooting &mdash; when the device does not enumerate or streaming stalls.
* @ref linux_usb &mdash; USB bandwidth budget, transfer tuning, autosuspend.
* @ref development &mdash; running the test suite, sanitizers, and Python tooling.
* @ref configuration &mdash; environment variables and runtime configuration.
