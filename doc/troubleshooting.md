# Troubleshooting {#troubleshooting}

Start with the symptom that matches yours. For questions about what the data
means rather than why it is missing, see the @ref faq.

The upstream
[troubleshooting wiki](https://github.com/OpenKinect/libfreenect2/wiki/Troubleshooting)
still holds useful hardware-specific notes and applies to this fork as well.

## No device connected

`Protonect` prints `no device connected!` and `Freenect2::enumerateDevices()`
returns 0.

1. **Check the port is USB 3.0.** USB 2 is not supported at all. Prefer a port
   directly on the machine over a hub, and avoid front-panel headers.
2. **Check the host controller.** Intel and NEC controllers are known to work;
   ASMedia controllers are known not to work. On Linux, `lsusb -t` and `lspci`
   identify the controller.
3. **Check the power supply.** The Kinect v2 needs its dedicated AC adapter, not
   just the USB connection. A white LED on the sensor indicates it is powered.
4. **Linux: install the udev rules.** Without them the device is only accessible
   as root:
   ```sh
   sudo cp platform/linux/udev/90-kinect2.rules /etc/udev/rules.d/
   ```
   Then unplug and replug the sensor. As a quick check, run `sudo Protonect` —
   if that works and the unprivileged run does not, it is a permissions problem.
5. **Windows: check the USB driver.** libfreenect2 needs UsbDk or libusbK bound
   to the composite parent device; the stock SDK driver will not do. See
   @ref install_windows.
6. **Virtual machines.** USB 3.0 isochronous transfer rarely survives a VM's USB
   passthrough. Run on bare metal.

## Streaming stalls, times out, or drops frames

* `waitForNewFrame()` times out, or the log shows packet-loss warnings.
* **Reduce contention on the bus.** The sensor needs most of a USB 3.0
  controller's bandwidth. Move other high-bandwidth devices to a different
  controller.
* **Linux: increase the USBFS buffer**, especially with more than one device.
  See
  [increasing usbfs buffer size](https://github.com/OpenKinect/libfreenect2/wiki/Troubleshooting#multiple-kinects-try-increasing-usbfs-buffer-size).
* **Tune the USB transfer parameters** with `LIBFREENECT2_RGB_TRANSFER_SIZE`,
  `LIBFREENECT2_RGB_TRANSFERS`, `LIBFREENECT2_IR_PACKETS`, and
  `LIBFREENECT2_IR_TRANSFERS`. Only do this if you know what the values mean;
  see @ref configuration.
* **Try the CPU pipeline** to rule out a GPU backend:
  `LIBFREENECT2_PIPELINE=cpu ./Protonect`. If CPU is stable and a GPU pipeline is
  not, the problem is in the depth backend, not USB.
* **Check timing semantics before concluding frames are dropped.** Device and
  arrival clocks differ; see @ref frame_timing.

## The pipeline I asked for is not being used

libfreenect2 logs `` `NAME' pipeline is not available. `` and falls through to
the next usable backend, ending at `cpu`.

* **Use a canonical name.** `LIBFREENECT2_PIPELINE` accepts `cpu`, `metal`,
  `opengl`, `opencl`, `opencl_kde`, `cuda`, `cuda_kde`, and `dump`. The older
  `gl` and `cl` spellings are accepted only as aliases through this environment
  variable.
* **Check it was compiled in.** A pipeline is only present if its `ENABLE_*`
  option found its dependencies at configure time. The CMake configure summary
  lists what was enabled.
* **Check there is a usable runtime device.** Each GPU pipeline is probed before
  it is accepted; a driver that is installed but has no usable device is skipped
  rather than crashing.
* **Ask the device what it got.** The opened device reports the pipeline it
  actually consumed, so you can assert on it instead of guessing.

## macOS: architecture mismatch at link time

```
building for macOS-x86_64 but attempting to link with file built for macOS-arm64
```

or many missing libusb/GLFW symbols. CMake detects this at configure time and
stops with instructions. Run `arch` — it must print `arm64` on Apple Silicon. A
shell, IDE, or CMake launched under Rosetta produces x86_64 builds that cannot
link Homebrew's arm64 libraries in `/opt/homebrew`. Full detail in
@ref install_macos.

## macOS: crash inside the RGB decoder on Apple Silicon

The VideoToolbox decoder crashes on M1 and later. `ENABLE_VIDEOTOOLBOX` already
defaults to `OFF` on Apple Silicon so TurboJPEG is used instead; if you forced
it `ON`, turn it back off.

## Multiple Kinects

Up to 5 devices have been reported working using multiple separate PCI Express
USB3 expansion cards with NEC controller chips.

* Do not plug an expansion card into a PCIe x1 slot — one lane does not have
  enough bandwidth. Use x8 or x16.
* On Linux, increase the USBFS buffer; larger device counts need larger buffers.
* See @ref recording_replay for opening and pairing several devices in software.

## Depth values look wrong

Depth accuracy problems are usually calibration, not a driver fault.

* Systematic offset, warm-up drift, and the limits of the factory calibration
  are covered in @ref depth_accuracy.
* To fit and apply a per-device linear correction, see @ref depth_calibration.
* For mapping between color, depth, and 3-D coordinates, see @ref registration.

## Log spam

Set the default logging level with `LIBFREENECT2_LOGGER_LEVEL`, or install a
custom `libfreenect2::Logger`. See @ref configuration and the @ref faq.

## Reporting a bug

Open an issue at
<https://github.com/hbmartin/libfreenect2-metal/issues>.

For USB problems, attach:

* The program's output with `LIBUSB_DEBUG=3` set.
* The relevant `dmesg` output (Linux).
* Hardware information: `lspci` and `lsusb -t` (Linux).
* Your OS and version, the pipeline in use, and the CMake configure summary.

```sh
LIBUSB_DEBUG=3 ./Protonect 2>&1 | tee protonect.log
```

## Next steps

* @ref install_macos, @ref install_linux, @ref install_windows &mdash; platform install guides.
* @ref development &mdash; building the test suite and running sanitizers to isolate a fault.
