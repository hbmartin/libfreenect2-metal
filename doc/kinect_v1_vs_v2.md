# Kinect v1 versus Kinect v2 {#kinect_v1_vs_v2}

**This library drives the Kinect for Windows v2 only.** If you have an Xbox 360
Kinect or a Kinect for Windows v1, nothing here will work for you — use
[libfreenect](https://github.com/OpenKinect/libfreenect) instead.

The two sensors share a brand and almost nothing else. They use different
depth-sensing physics, different USB generations, different wire protocols,
different resolutions, and different units. This page exists because the
confusion is common and because a great deal of Kinect material online — much of
it from 2011 — describes v1 without saying so.

## Which one do you have?

The quickest check is physical: **the v2 requires a separate AC power adapter**
and has a bulky powered USB 3.0 adapter brick. A v1 for Xbox 360 has a tilt
motor in its base and audibly whirrs when it moves; the v2 base is fixed.

By USB ID:

| Sensor | Vendor:Product |
|---|---|
| Kinect v2 (K4W2, Xbox One) | `045e:02c4`, `045e:02d8`, `045e:02d9` |
| Kinect v1 (Xbox 360) — motor | `045e:02b0` |
| Kinect v1 (Xbox 360) — audio | `045e:02ad` |

```sh
lsusb | grep 045e                # Linux
system_profiler SPUSBDataType    # macOS: look for "Xbox NUI Sensor"
```

The v2 IDs are the ones this project's udev rules match
(`platform/linux/udev/90-kinect2.rules`). If none of the v2 IDs appear but a v1
ID does, you are in the wrong repository.

The v1 also enumerates as three separate USB devices behind an internal hub
(camera, motor, audio); the v2 presents one device with two interface
associations. See @ref protocol for the v2 topology.

## How they differ

| | Kinect v1 | Kinect v2 |
|---|---|---|
| Depth principle | Structured light — an 830 nm laser projects a fixed speckle pattern; depth comes from pattern distortion | Time of flight — amplitude-modulated IR; depth comes from phase |
| Depth output | 11-bit **disparity**, needs conversion to metric | `float` **millimeters**, directly usable |
| Depth resolution | 640×480 (rightmost 8 columns always invalid) | 512×424 |
| Invalid pixel | `2047` | `0` (also non-positive, NaN, infinity) |
| IR output | 640×488 | 512×424 `float`, range [0, 65535] |
| Color output | 640×480 Bayer at 30 Hz, or 1280×1024 at 15 Hz | 1920×1080, JPEG on the wire, BGRX/RGBX decoded |
| Color and IR together | Impossible — one isochronous stream, one mode | Both stream simultaneously |
| Field of view | 58° H × 45° V | ≈70° H × 60° V |
| Usable range | 0.8–3.5 m | 0.5–4.5 m |
| USB | 2.0 | 3.0 SuperSpeed, required |
| Power | From USB, plus adapter for Xbox models | Dedicated AC adapter, mandatory |
| Tilt motor | Yes, ±31° | No |
| Accelerometer | Yes (KXSD9) | Not exposed |
| Status LED | Yes, 7 fixed colors/patterns | Yes, 2 LEDs, intensity 0–1000, constant or blink |
| Microphones | 4, with firmware upload and hardware noise cancellation | 4, not supported by this library — see @ref faq |

The v2 field of view is approximate: it is not reported by the device, but
follows from the factory IR intrinsics, where a typical `fx` near 365 over a
512×424 image gives roughly 70° × 60°.

## Why v1 answers mislead

Search results for "Kinect depth" skew heavily toward v1. These are the specific
things that do **not** carry over:

* **Disparity conversion formulas.** Expressions like
  `100/(-0.00307 * d + 3.33)` or `0.1236 * tan(d/2842.5 + 1.1863)` convert v1's
  raw 11-bit disparity to distance. The v2 reports millimeters directly; running
  depth values through these produces nonsense.
* **The `2047` sentinel.** On v2, invalid pixels are `0`. Testing for `2047`
  silently keeps bad data and discards good data.
* **Tilt and LED control transfers.** The v1 control transfers for tilt
  (`0x40 0x31`), LED (`0x40 0x06`), and the 10-byte accelerometer report do not
  exist on v2. The v2 has no motor at all; its LED interface is a completely
  different command. See @ref protocol.
* **`freenect_*` API names.** `freenect_sync_get_depth()`,
  `freenect_set_tilt_degs()`, `FREENECT_LOG_SPEW` and friends belong to
  libfreenect. This library's API is C++ and namespaced `libfreenect2::`.
* **Interference between two sensors.** Two v1 sensors degrade each other by
  overlaying speckle patterns; advice about polarization tricks addresses that
  mechanism. Two v2 sensors interact differently, because the failure mode is
  modulated-light interference rather than pattern confusion.
* **USB 2.0 packet arithmetic.** Figures like "242 packets of 1760 bytes per
  depth frame" describe v1's isochronous framing. The v2's framing is in
  @ref protocol.
* **Sensor part numbers.** The MT9M001 / MT9V112 imagers, the PrimeSense PS1080,
  and the Marvell audio SoC are v1 hardware. The v2 shares none of them.

## Provenance of this page

The v1 figures come from the OpenKinect project wiki, which documented
libfreenect between 2010 and roughly 2012. That wiki lived at `openkinect.org`;
**the domain has since lapsed and now serves unrelated advertising — do not
visit it expecting documentation.** Archived copies remain reachable through the
Internet Archive.

The v2 figures come from this repository's source and from the
[libfreenect2 wiki](https://github.com/OpenKinect/libfreenect2/wiki).

## Next steps

* @ref protocol &mdash; the Kinect v2 wire protocol.
* @ref troubleshooting &mdash; if you have a v2 and it is not enumerating.
* @ref depth_accuracy &mdash; what the v2's millimetre values are actually worth.
