# Installing on Windows {#install_windows}

This is the reference install path for Windows with Visual Studio. For a
condensed version see the
[README](https://github.com/hbmartin/libfreenect2-metal#readme).

If you only need a binary and are not modifying libfreenect2, see
[vcpkg](#vcpkg) at the bottom — it is far less work.

## Requirements

* A USB 3.0 controller. USB 2 is not supported. Intel and NEC host controllers
  are known to work; ASMedia controllers are known not to work.
* Windows 8 or newer. Windows 7 works but is buggy.
* Visual Studio with the C++ toolset, and CMake.

Virtual machines usually do not work, because USB 3.0 isochronous transfer is
delicate.

## Step 1: install a USB driver

libfreenect2 talks to the sensor through libusb, which needs a compatible
backend driver. Pick **either** UsbDk **or** libusbK — do not install both.

Neither interferes with the Kinect for Windows v2 SDK; you do not need the SDK
to build libfreenect2, and you do not need to uninstall it first.

### Option A: UsbDk (recommended)

1. **Windows 7 only:** install Microsoft Security Advisory 3033929 first,
   otherwise your USB keyboard and mouse will stop working.
2. Download the latest x64 installer from
   <https://github.com/daynix/UsbDk/releases> and install it.
3. If UsbDk does not work, uninstall it and use libusbK instead.

### Option B: libusbK

Follow these steps exactly:

1. Download Zadig from <http://zadig.akeo.ie/>.
2. In Zadig's options, check **List All Devices** and uncheck **Ignore Hubs or
   Composite Parents**.
3. Select **Xbox NUI Sensor (composite parent)** from the drop-down. Ignore the
   "NuiSensor Adaptor" entries — those are the adapter, not the Kinect. The
   current driver will be listed as `usbccgp`; the USB ID is VID `045E`, PID
   `02C4` or `02D8`.
4. Select **libusbK** (v3.0.7.0 or newer) from the replacement driver list.
5. Click **Replace Driver** and accept the warning about replacing a system
   driver (it appears because this is a composite parent).

#### Uninstalling libusbK

1. Open **Device Manager**.
2. Under **libusbK USB Devices**, right-click **Xbox NUI Sensor (Composite
   Parent)** and select **Uninstall**.
3. Check **Delete the driver software for this device**, then click OK.
4. To restore the official SDK driver, choose **Action > Scan for hardware
   changes**. The sensor is re-enumerated with the K4W2 SDK driver and
   `KinectService.exe` works again immediately.

You can switch between the SDK driver and libusbK freely with these steps.

## Step 2: install dependencies

### libusb (required)

Download the latest build (`.7z`) from
<https://github.com/libusb/libusb/releases> and extract it as `depends/libusb`
(rename the `libusb-1.x.y` folder to `libusb`).

### TurboJPEG (required for RGB decoding)

Download the `-vc64.exe` installer from
<http://sourceforge.net/projects/libjpeg-turbo/files> and extract it to
`c:\libjpeg-turbo64` (the installer default), to `depends/libjpeg-turbo64`, or
anywhere pointed to by the `TurboJPEG_ROOT` environment variable.

### GLFW (required for the example viewer)

Download the 64-bit build from <http://www.glfw.org/download.html> and extract it
as `depends/glfw` (rename `glfw-3.x.x.bin.WIN64` to `glfw`), or anywhere pointed
to by the `GLFW_ROOT` environment variable.

A library-only build can skip GLFW with `-DBUILD_EXAMPLES=OFF`.

### OpenCL (optional)

Intel GPU: download "Intel® SDK for OpenCL™ Applications 2016" from
<https://software.intel.com/en-us/intel-opencl> (free registration required) and
install it. Requires OpenCL 1.1 or newer.

### CUDA (optional, NVIDIA only)

Download and install the CUDA Toolkit. The **samples package is not required** —
the `cuda` and `cuda_kde` pipelines depend only on Toolkit headers and
libraries.

### OpenNI2 (optional)

Download OpenNI 2.2.0.33 (x64) from
<https://github.com/structureio/OpenNI2/releases> and install it to the default
location under `C:\Program Files`. The former structure.io/openni download page
is gone.

## Step 3: build

The default install path is `install`; change it via `CMAKE_INSTALL_PREFIX`.

```
mkdir build && cd build
cmake .. -G "Visual Studio 16 2019"
cmake --build . --config RelWithDebInfo --target install
```

Older toolsets also work, e.g. `-G "Visual Studio 14 2015 Win64"` or
`-G "Visual Studio 12 2013 Win64"`.

## Step 4: run

```
.\install\bin\Protonect.exe
```

Or start debugging from Visual Studio.

## Testing OpenNI2 (optional)

Copy `freenect2-openni2.dll` and the other DLLs from `install\bin`
(`libusb-1.0.dll`, `glfw.dll`, and so on) to
`C:\Program Files\OpenNI2\Tools\OpenNI2\Drivers`, then run
`C:\Program Files\OpenNI2\Tools\NiViewer.exe`.

Set `LIBFREENECT2_PIPELINE` to select a pipeline, for example `opencl` or
`cuda`.

## vcpkg

You can download and install libfreenect2 using the
[vcpkg](https://github.com/Microsoft/vcpkg) dependency manager:

```
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
./vcpkg integrate install
vcpkg install libfreenect2
```

The libfreenect2 port in vcpkg is maintained by Microsoft team members and
community contributors, and tracks **upstream** `OpenKinect/libfreenect2`, not
this fork. If the version is out of date, please
[create an issue or pull request](https://github.com/Microsoft/vcpkg) on the
vcpkg repository.

## Next steps

* @ref troubleshooting &mdash; when the device does not enumerate or streaming stalls.
* @ref development &mdash; running the test suite, sanitizers, and Python tooling.
* @ref configuration &mdash; environment variables and runtime configuration.
