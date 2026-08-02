# Troubleshooting {#troubleshooting}

A diagnostic flow, ordered so that each stage rules out a class of problem
before the next. Work top to bottom; most reports resolve in the first two
sections.

For questions about what the data *means* rather than why it is missing, see the
@ref faq. For USB bandwidth theory and transfer tuning, see @ref linux_usb.

> Signatures marked **(Linux)** below are drawn from the upstream issue corpus
> and the
> [upstream troubleshooting wiki](https://github.com/OpenKinect/libfreenect2/wiki/Troubleshooting),
> which was last edited in 2017. They were not reproducible on this project's
> macOS validation host. Treat them as leads, not guarantees.

## 1. Device absent

`Protonect` prints `no device connected!`, or `Freenect2::enumerateDevices()`
returns 0.

### Power

The Kinect v2 requires its dedicated AC adapter — the USB connection alone will
not power it. A lit white LED on the sensor means it is powered. Replug both the
adapter and the USB cable, and give the device a few seconds to enumerate.

### USB topology

USB 2 is not supported at all, and the link must actually negotiate SuperSpeed:

```sh
lsusb -t                        # Linux: the Kinect's port must show 5000M
system_profiler SPUSBDataType   # macOS: "Xbox NUI Sensor", "Speed: Up to 5 Gb/s"
```

Anything below `5000M` means a bad cable, a USB 2 port, or a marginal link.
Connect directly to a port on the machine — not through a hub, and preferably not
a front-panel header.

Identify the controller before reporting a problem:

```sh
lspci -nn | grep -i usb
```

Intel and NEC controllers are widely reported to work. ASMedia controllers are
widely reported not to work; if yours is ASMedia, try a different controller
before spending time elsewhere.

### Permissions (Linux)

Without the udev rule the device nodes are root-only. Install it:

```sh
sudo cp platform/linux/udev/90-kinect2.rules /etc/udev/rules.d/
```

Then unplug and replug the sensor. The rule sets `MODE="0666"` on Microsoft
vendor `045e`, products `02c4`, `02d8`, and `02d9`.

To confirm it applied, check the node's mode rather than re-running the viewer
as root:

```sh
lsusb -d 045e:02c4          # e.g. Bus 002 Device 005: ID 045e:02c4 ...
ls -l /dev/bus/usb/002/005  # want crw-rw-rw-
```

Do not diagnose by running `Protonect` under `sudo`. It can succeed or fail for
reasons unrelated to USB permissions — under a desktop session the GL/GLFW
context may not open for the root user at all — so the result is ambiguous
either way, and it can leave root-owned files behind. Checking the node mode
answers the same question unambiguously.

### Interface already owned

At open, libfreenect2 now rejects a known USB 2 link before resetting the
sensor and reports when a kernel driver owns either video interface. It never
detaches that driver automatically. A `LIBUSB_ERROR_BUSY` claim failure can
also mean that another Kinect, OpenNI, or Kinect SDK process already has the
interface open. Stop the competing process or service, confirm the link is
still `5000M` with `lsusb -t`, and retry. On Linux, `sudo fuser -v
/dev/bus/usb/BBB/DDD` can identify processes holding the device node.

### Virtual machines

USB 3.0 isochronous transfer rarely survives a VM's USB passthrough. Run on bare
metal before investigating anything else.

### Windows

libfreenect2 needs UsbDk or libusbK bound to the composite parent device; the
stock Kinect SDK driver will not work. See @ref install_windows.

## 2. Device opens but streaming fails

The device enumerates and opens, then `waitForNewFrame()` times out or frames
arrive with gaps.

### Establish a CPU baseline first

Before blaming USB, remove the GPU from the picture:

```sh
LIBFREENECT2_PIPELINE=cpu Protonect -noviewer -frames 30
```

If CPU is stable and a GPU pipeline is not, the fault is in the depth backend —
skip to section 3, *A backend fails*. If CPU also stalls, the problem is USB or
the device, and belongs in this section.

Narrow further by disabling one stream:

```sh
Protonect -noviewer -frames 30 -norgb     # depth/IR only
Protonect -noviewer -frames 30 -nodepth   # color only
```

If either stream alone is stable but both together are not, the controller does
not have the bandwidth for the pair. That is a topology problem, not a tuning
problem.

### Controller bandwidth

A single Kinect v2 consumes most of a USB 3.0 controller. Move other
high-bandwidth devices to a different controller, and never share a controller
between two sensors. See @ref linux_usb for the full budget discussion.

### `dmesg` signatures (Linux)

```sh
sudo dmesg -w   # in a second terminal, while starting Protonect
```

| Message | Severity | Meaning |
|---|---|---|
| `xhci_hcd … ERROR Transfer event TRB DMA ptr not part of current TD` | Usually harmless | Common xHCI chatter during isochronous streaming; not by itself a fault |
| `xhci_hcd … WARN Event TRB for slot … with no TDs queued` | Usually harmless | Same class of noise |
| `Not enough bandwidth for new device state` | Fatal | The controller cannot admit the isochronous endpoint. Move the sensor to its own controller. |
| `device descriptor read/64, error -110` | Fatal | Timeout. Cable, power, or port. |
| `usb … reset SuperSpeed USB device number … using xhci_hcd` | Investigate | A single reset at start can be normal; repeated resets mean an unstable link |
| Allocation failure / `-ENOMEM` on submit | Fatal | The usbfs memory limit is too low. See below. |

The distinction that matters: the first two appear constantly on healthy systems
and are not worth reporting on their own, whereas bandwidth and `-110` errors
are conclusive.

### usbfs memory limit (Linux)

The kernel caps memory pinned for USB transfers, and the default is often too
low:

```sh
cat /sys/module/usbcore/parameters/usbfs_memory_mb
echo 1000 | sudo tee /sys/module/usbcore/parameters/usbfs_memory_mb
```

This does not survive a reboot; see @ref linux_usb to persist it. The limit does
not exist on macOS or Windows.

### Autosuspend (Linux)

Linux may suspend an idle sensor mid-session. Scope the fix to the Kinect rather
than disabling autosuspend for every USB device on the machine, and note that it
reverses with `auto` or a replug. Procedure in @ref linux_usb.

### Transfer pool tuning

A last resort, after topology and the usbfs limit. The four
`LIBFREENECT2_*_TRANSFERS` / `*_PACKETS` variables and their platform defaults
are documented in @ref linux_usb and @ref configuration. No pool size
compensates for two sensors on one controller.

### Before concluding frames are dropped

Device and arrival clocks differ, and color and depth are not hardware
synchronized. Check @ref frame_timing for what the timestamps mean and what
pairing guarantees actually exist.

## 3. A backend fails

The requested pipeline is not used, or depth output is wrong on one backend and
right on another.

### The pipeline you asked for is not being used

libfreenect2 logs a warning and falls through the chain
`metal → opengl → cuda → opencl → cpu`, probing each for a usable runtime
device:

```
[Warning] [createDefaultPacketPipeline(…)] `bogus' pipeline is not available.
```

* **Use a canonical name.** `LIBFREENECT2_PIPELINE` accepts `cpu`, `metal`,
  `opengl`, `opencl`, `opencl_kde`, `cuda`, `cuda_kde`, and `dump`. The older
  `gl` and `cl` spellings are accepted only as aliases through this environment
  variable.
* **Check it was compiled in.** A pipeline exists only if its `ENABLE_*` option
  found its dependencies at configure time; the CMake configure summary lists
  what was enabled. At runtime, `getCompiledPacketPipelines()` returns the
  canonical names built into the library and `getAvailablePacketPipelines()`
  returns those usable on this machine.
* **Ask the device what it got.** The opened device reports the pipeline it
  actually consumed, so you can assert on it rather than guessing.

### Per-backend checks

| Backend | Check |
|---|---|
| Metal | macOS only. The log names the GPU it selected, e.g. `MetalDepthPacketProcessor: using device Apple M4 Pro`. |
| OpenGL | Needs OpenGL 3.1; OpenGL ES is unsupported. Deprecated by Apple — prefer `metal` there. |
| OpenCL | Verify the ICD stack with `clinfo` before suspecting libfreenect2. A driver that is installed but exposes no usable device is skipped, not fatal. |
| CUDA | Hosted CI compiles the CUDA pipelines but does not validate them at runtime. Compare CUDA against CPU output on your own hardware. |
| VAAPI | Intel, Ivy Bridge or newer, Linux only. Select an explicit node with `LIBFREENECT2_VAAPI_DEVICE=/dev/dri/renderD128` if autodetection picks the wrong one. |

### macOS: crash inside the RGB decoder on Apple Silicon

The VideoToolbox decoder crashes on M1 and later. `ENABLE_VIDEOTOOLBOX` already
defaults to `OFF` on Apple Silicon so TurboJPEG is used instead; if you forced it
`ON`, turn it back off.

### macOS: architecture mismatch at link time

```
building for macOS-x86_64 but attempting to link with file built for macOS-arm64
```

or many missing libusb/GLFW symbols. CMake detects this at configure time and
stops with instructions. Run `arch` — it must print `arm64` on Apple Silicon. A
shell, IDE, or CMake launched under Rosetta produces x86_64 builds that cannot
link Homebrew's arm64 libraries in `/opt/homebrew`. Full detail in
@ref install_macos.

## 4. Multiple Kinects

* **One sensor per USB3 host controller.** This is the binding constraint; two
  on one controller usually means dropped transfers.
* **No hubs.** A hub multiplexes onto one upstream link, so the sensors still
  share a budget.
* **Expansion cards need lanes.** x8 or x16, never x1.
* **Raise the usbfs limit** on Linux, further for each additional device.
* On Apple Silicon, each Thunderbolt/USB4 port generally has its own controller,
  so one sensor per physical port typically works.

See @ref recording_replay for the software side, and @ref linux_usb for why the
one-per-controller rule exists.

## 5. Depth values look wrong

Depth accuracy problems are usually calibration, not a driver fault.

* Systematic offset, warm-up drift, and the limits of the factory calibration
  are covered in @ref depth_accuracy.
* To fit and apply a per-device linear correction, see @ref depth_calibration.
* For mapping between color, depth, and 3-D coordinates, see @ref registration.

## Known-harmless messages

### `closed with shutdown errors` on macOS

Every clean shutdown on macOS currently logs:

```
[Info] [Freenect2DeviceImpl] releasing usb interfaces...
[Error] [protocol::UsbControl] failed to release interface with ControlAndRgbInterfaceId(=0)!
        LIBUSB_ERROR_NO_DEVICE No such device (it may have been disconnected).
[Info] [Freenect2DeviceImpl] closed with shutdown errors
```

This occurs during teardown, after streaming has finished, because the device has
already dropped off the bus by the time the interface is released. It is
reproducible on every run and does not indicate lost or corrupted frames —
captures complete normally. It is safe to ignore unless it appears *before*
`stopped`.

### Log spam

Set the default level with `LIBFREENECT2_LOGGER_LEVEL` (`debug`, `info`,
`warning`, `error`, `none`), or install a custom `libfreenect2::Logger`. See
@ref configuration and the @ref faq.

## Reporting a bug

Open an issue at <https://github.com/hbmartin/libfreenect2-metal/issues>.

A complete report includes:

| Item | Command |
|---|---|
| Exact command run | — |
| Commit | `git log -1 --oneline` |
| OS and kernel | `uname -a` / `sw_vers` |
| USB topology and link speed | `lsusb -t` / `system_profiler SPUSBDataType` |
| Host controller ID | `lspci -nn \| grep -i usb` |
| Kernel messages | `dmesg` from around the failure |
| Pipeline in use | the `LIBFREENECT2_PIPELINE` value, or the pipeline the device reported |
| GPU and driver version | — |
| libusb diagnostics | `LIBUSB_DEBUG=3` output |

```sh
LIBUSB_DEBUG=3 Protonect -noviewer -frames 30 2>&1 | tee protonect.log
```

Note any system-wide changes you have already made — kernel boot parameters,
`vm.min_free_kbytes`, autosuspend settings — since they change the baseline your
results are measured against.

## Next steps

* @ref install_macos, @ref install_linux, @ref install_windows &mdash; platform install guides.
* @ref linux_usb &mdash; USB bandwidth budget and transfer tuning.
* @ref test_plan &mdash; the smoke checklist used to validate a build.
* @ref development &mdash; building the test suite and running sanitizers to isolate a fault.
