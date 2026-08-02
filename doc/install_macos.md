# Installing on macOS {#install_macos}

[TOC]

This is the reference install path for macOS, including Apple Silicon. For a
condensed version see the [README](https://github.com/hbmartin/libfreenect2-metal#readme).

## Requirements

* A USB 3.0 port. USB 2 is not supported.
* Xcode command line tools (`xcode-select --install`).
* Homebrew, MacPorts, or an equivalent package manager.

Apple Silicon (M1 and later) is supported with native arm64 builds and the
`metal` depth pipeline. Intel Macs are supported through the `opengl`, `opencl`,
and `cpu` pipelines.

## Build tools

Install the tools first. Xcode may already provide some of them.

```sh
brew install cmake pkg-config git wget ninja
```

## Source

```sh
git clone https://github.com/hbmartin/libfreenect2-metal.git
cd libfreenect2-metal
```

Cloning `OpenKinect/libfreenect2` instead gets you upstream, which has no Metal
pipeline and none of the 0.4 APIs described in the guides.

## Dependencies

Required:

```sh
brew update
brew install libusb glfw3
```

Optional:

```sh
# TurboJPEG — the default RGB decoder on Apple Silicon
brew install jpeg-turbo
```

`glfw3` is only needed for the example viewer; a library-only build can use
`-DBUILD_EXAMPLES=OFF`.

### OpenNI2 (optional)

The old `brewsci/science` bottle is no longer downloadable (the hosted archive
returns 404), so build OpenNI2 from source:

```sh
git clone https://github.com/structureio/OpenNI2.git
cd OpenNI2
make release
# then point libfreenect2 at the build output, e.g.:
export OPENNI2_REDIST=$PWD/Bin/x64-Release
export OPENNI2_INCLUDE=$PWD/Include
```

### CUDA

Not available on macOS. `ENABLE_CUDA` has no effect here.

## Build and install

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build
```

The default install prefix is CMake's platform default; pass
`-DCMAKE_INSTALL_PREFIX=$HOME/freenect2` to install somewhere writable without
`sudo`.

Run the test program:

```sh
./build/bin/Protonect
```

## Apple Silicon notes

### Metal is the default GPU pipeline

`ENABLE_METAL` defaults to `ON` on Apple platforms. When no pipeline is
requested, `createDefaultPacketPipeline()` probes Metal first and falls through
to the next available backend, ending at `cpu`. OpenGL is deprecated by Apple;
prefer `metal`.

### VideoToolbox is off by default on Apple Silicon

The VideoToolbox RGB decoder crashes on M1 and later, so `ENABLE_VIDEOTOOLBOX`
defaults to `OFF` there and RGB decoding uses TurboJPEG. On Intel Macs it
defaults to `ON`. See `ENABLE_VIDEOTOOLBOX` in `CMakeLists.txt`.

### Architecture mismatch

The most common Apple Silicon failure is an architecture mismatch between the
build and the installed libraries:

```
building for macOS-x86_64 but attempting to link with file built for macOS-arm64
```

or a wall of missing libusb/GLFW symbols at link time. CMake detects this at
configure time and stops with instructions. To avoid it:

* **Use a native arm64 terminal.** Run `arch`; it must print `arm64`, not
  `i386`. A shell, IDE, or CMake launched under Rosetta produces x86_64 builds
  that cannot link Homebrew's arm64 libraries in `/opt/homebrew`.
* **Use the matching Homebrew prefix:** `/opt/homebrew` on Apple Silicon,
  `/usr/local` on Intel. If CMake picks the wrong one, pass
  `-DCMAKE_PREFIX_PATH=/opt/homebrew`.
* **Do not force `-DCMAKE_OSX_ARCHITECTURES=x86_64`** unless every dependency is
  also x86_64.

## Device access

macOS needs no udev-style rules. If the device is not enumerated, unplug it,
wait a few seconds, and replug it into a USB 3.0 port directly on the machine
rather than through a hub.

## Testing OpenNI2 (optional)

```sh
sudo cmake --build build --target install-openni2
NiViewer
```

Set `LIBFREENECT2_PIPELINE` to select a pipeline, for example
`LIBFREENECT2_PIPELINE=metal NiViewer`.

## Next steps

* @ref troubleshooting &mdash; when the device does not enumerate or streaming stalls.
* @ref development &mdash; running the test suite, sanitizers, and Python tooling.
* @ref configuration &mdash; environment variables and runtime configuration.
