# Installing on Linux {#install_linux}

This is the reference install path for Linux. For a condensed version see the
[README](https://github.com/hbmartin/libfreenect2-metal#readme).

Instructions target current Debian and Ubuntu. Other distributions work; adapt
the package names. See [Legacy distributions](#legacy-distributions) below if
you are stuck on Ubuntu 14.04.

## Requirements

* A USB 3.0 controller. USB 2 is not supported. Intel and NEC host controllers
  are known to work; ASMedia controllers are known not to work.
* Kernel 3.16 or newer; as new as possible is better.
* libusb 1.0.20 or newer.

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

On Ubuntu 14.04 to 16.04 the TurboJPEG packages are named
`libturbojpeg libjpeg-turbo8-dev` instead.

## Optional dependencies

### OpenGL (example viewer and the `opengl` pipeline)

```sh
sudo apt-get install libglfw3-dev
```

OpenGL 3.1 is required. On platforms that lack it (for example the Odroid XU4)
configure with `-DENABLE_OPENGL=OFF`. OpenGL ES is not supported.

### OpenCL

* **Intel GPU:** `sudo apt-get install beignet-dev`. On older kernels
  `# echo 0 >/sys/module/i915/parameters/enable_cmd_parser` is needed. See the
  [Beignet known issues](https://www.freedesktop.org/wiki/Software/Beignet/).
* **AMD GPU:** install the latest AMD Catalyst drivers from
  <https://support.amd.com>, then `sudo apt-get install opencl-headers`.
* **Mali GPU (e.g. Odroid XU4):** as root,
  ```sh
  mkdir -p /etc/OpenCL/vendors
  echo /usr/lib/arm-linux-gnueabihf/mali-egl/libmali.so >/etc/OpenCL/vendors/mali.icd
  apt-get install opencl-headers
  ```
* **Verify:** install `clinfo` to confirm the OpenCL stack is set up correctly.

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
validation. Compare CUDA and CPU output on real hardware before a release.

Notes:

* **Jetson TK1:** CUDA is preloaded. Requires Linux4Tegra 21.3 or later; check
  the [Jetson TK1 issues](https://github.com/OpenKinect/libfreenect2/wiki/Troubleshooting#jetson-tk1-issues)
  before installing. Jetson TX1 is not yet supported.
* **NVIDIA/Intel dual GPUs:** after installing CUDA, use `sudo prime-select intel`
  to keep the Intel GPU for the desktop.

### VAAPI JPEG decoding (Intel only)

Requires Ivy Bridge or newer.

```sh
sudo apt-get install libva-dev libjpeg-dev
```

Linux kernels 4.1 to 4.3 have a performance regression. Use 4.0 and earlier or
4.4 and later. (Ubuntu kernel 4.2.0-28.33~14.04.1 has the fix backported.)

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

Then unplug and replug the Kinect.

## Run

```sh
./build/bin/Protonect
```

## Multiple Kinects

Up to 5 devices have been reported working on a high-end PC using multiple
separate PCI Express USB3 expansion cards with NEC controller chips.

* [Increase the USBFS memory buffer](https://github.com/OpenKinect/libfreenect2/wiki/Troubleshooting#multiple-kinects-try-increasing-usbfs-buffer-size).
  More devices need a larger buffer.
* Do not plug an expansion card into a PCIe x1 slot — one lane does not have
  enough bandwidth. Use x8 or x16.

See @ref recording_replay for the software side of running several devices.

## Testing OpenNI2 (optional)

```sh
sudo apt-get install openni2-utils
sudo cmake --build build --target install-openni2
NiViewer2
```

Set `LIBFREENECT2_PIPELINE` to select a pipeline, for example
`LIBFREENECT2_PIPELINE=opencl NiViewer2`.

## Legacy distributions

Ubuntu 12.04 is too old to support. Debian jessie may also be too old.

Ubuntu 14.04 needs upgraded packages from the bundled download script. Run this
from the repository root, and `cd ..` back afterwards:

```sh
cd depends && ./download_debs_trusty.sh
```

Then, in place of the corresponding steps above:

| Component | Ubuntu 14.04 step |
|---|---|
| libusb | `sudo dpkg -i debs/libusb*deb` |
| OpenGL | `sudo dpkg -i debs/libglfw3*deb; sudo apt-get install -f` |
| OpenCL (Intel) | `sudo apt-add-repository ppa:floe/beignet; sudo apt-get update; sudo apt-get install beignet-dev; sudo dpkg -i debs/ocl-icd*deb` |
| CUDA | Download `cuda-repo-ubuntu1404...*.deb` ("deb (network)") from the NVIDIA website and follow their instructions, including `apt-get install cuda`, which installs the NVIDIA graphics driver. |
| VAAPI | `sudo dpkg -i debs/{libva,i965}*deb; sudo apt-get install -f` |
| OpenNI2 | `sudo apt-add-repository ppa:deb-rob/ros-trusty && sudo apt-get update` (skip if you have ROS repos), then `sudo apt-get install libopenni2-dev` |

## Next steps

* @ref troubleshooting &mdash; when the device does not enumerate or streaming stalls.
* @ref development &mdash; running the test suite, sanitizers, and Python tooling.
* @ref configuration &mdash; environment variables and runtime configuration.
