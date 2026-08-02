# Kinect v2 USB protocol {#protocol}

[TOC]

A reference for the wire protocol libfreenect2 speaks to the Kinect for Windows
v2. It is written for people modifying the driver, debugging a capture, or
porting the protocol elsewhere — not for application authors, who should use
the public API instead.

Everything here is derived from this repository's implementation
(`include/internal/libfreenect2/protocol/`, `src/usb_control.cpp`,
`src/command_transaction.cpp`, `src/depth_packet_stream_parser.cpp`,
`src/rgb_packet_stream_parser.cpp`). The device is undocumented by Microsoft;
the protocol was reverse-engineered from USB captures and, for the color and LED
settings, from Microsoft's MIT-licensed
[NuiSensorLib](https://github.com/Microsoft/MixedRealityCompanionKit/tree/master/KinectIPD/NuiSensor).
**Several commands are still unexplained**, and the sections below say so where
that is the case.

> The Kinect v1 protocol is completely different and nothing on this page
> applies to it. See @ref kinect_v1_vs_v2.

## USB topology

The sensor presents itself as a hub with two interface associations:

| Association | Purpose |
|---|---|
| 1 | Video transfer — enabled and suspended via the `FUNCTION_SUSPEND` feature |
| 2 | Audio transfer — not used by libfreenect2 (see @ref faq) |

The video association contains two interfaces:

| Interface | Endpoint | Type | Direction | Carries |
|---|---|---|---|---|
| 0 | `0x02` | Bulk | OUT | Command requests |
| 0 | `0x81` | Bulk | IN | Command responses |
| 0 | `0x83` | Bulk | IN | Color (JPEG) stream |
| 0 | `0x82` | Interrupt | IN | Unknown — not read by libfreenect2 |
| 1 | `0x84` | Isochronous | IN | IR/depth stream |

Interface 1 is enabled and disabled by switching its alternate setting between
1 and 0, which is how libfreenect2 starts and stops the depth stream without
tearing down the device.

On Linux the two audio interfaces are claimed by `snd-usb-audio`, so a healthy
`lsusb -t` shows the Kinect as a hub with two vendor-specific interfaces plus
two audio interfaces. That is expected and is not a conflict.

The isochronous endpoint's maximum packet size is read from its descriptor at
open time and must be at least `0x8400` (33792) bytes, or the open fails. See
@ref linux_usb for where that number comes from and what it costs in bandwidth.

## Command transactions

Commands go out on `0x02` and responses come back on `0x81`. Every command has
the same header, defined as `CommandData` in
`include/internal/libfreenect2/protocol/command.h`:

```c
struct CommandData {
  uint32_t magic;               // always 0x06022009
  uint32_t sequence;            // incrementing; echoed in the completion
  uint32_t max_response_length; // bytes the host is prepared to read
  uint32_t command;             // one of the KCMD_* values below
  uint32_t reserved0;
  uint32_t parameters[NParam];  // 0, 1, or 4 parameters
};
```

A transaction is three steps:

1. Bulk OUT the command on `0x02`.
2. Bulk IN the response payload on `0x81`, if `max_response_length` is nonzero.
3. Bulk IN a 16-byte **response completion** on `0x81`, whose first word is the
   magic `0x0A6FE000` and whose second word must match the command's `sequence`.

A completion arriving where the payload was expected, or one carrying the wrong
sequence number, is a protocol error and is logged as such. The transaction
timeout is 1000 ms.

### The magic number

`0x06022009` reads as the date 06/02/2009 — the day after Project Natal, the
Kinect's original codename, was announced at E3 2009. The same constant appears
as the bootloader magic in the **Kinect v1** audio firmware protocol, documented
on the now-defunct OpenKinect wiki. The v2 command protocol inherited it. It has
no functional meaning beyond framing.

The response completion magic `0x0A6FE000` likewise carried over from v1.

### Commands

| `KCMD_*` | Value | Parameters | Response | Purpose |
|---|---|---|---|---|
| `SHUTDOWN` | `0x00` | 0 | — | Observed in the shutdown sequence |
| `READ_FIRMWARE_VERSIONS` | `0x02` | 0 | `0x200` | Per-subsystem firmware versions |
| `INIT_STREAMS` | `0x09` | 0 | — | Arm streaming |
| `STOP` | `0x0A` | 0 | — | Observed in the stop sequence |
| `READ_HARDWARE_INFO` | `0x14` | 0 | `0x5C` | Response discarded — see below |
| `READ_STATUS` | `0x16` | 1 | `0x04` | Read a status word by address |
| `READ_DATA_PAGE` | `0x22` | 1 | varies | Serial number, calibration, P0 tables |
| `READ_DATA_0x26` | `0x26` | 0 | `0x10` | Unexplained; issued during start |
| `SET_STREAMING` | `0x2B` | 1 | — | 0 disables, 1 enables streaming |
| `RGB_SETTING` | `0x3E` | 4 | 16 | Color camera settings |
| `0x46` | `0x46` | 4 | — | Unexplained; currently commented out |
| `0x47` | `0x47` | 0 | `0x10` | Unexplained; currently commented out |
| `SET_MODE` / `LED_SETTING` | `0x4B` | 4 | — | Shares one command value; see below |

`0x4B` is overloaded: with the mode parameters it gates streaming state, and
with the LED payload it drives the two status LEDs. The driver distinguishes
them only by the parameters it sends.

`READ_HARDWARE_INFO` is issued during start, but its response is thrown away.
The hardware version it reports is only useful for selecting the correct **IR
normalization table**, and libfreenect2 does not have those tables — which is
one reason its depth values differ slightly from the Microsoft SDK's. See
@ref depth_accuracy.

Four `READ_STATUS` addresses are defined in `command.h`. Only `0x090000` (device
readiness) and `0x100007` are actually sent; `0x02006F` and `0x020070` are
declared but never used, and their meaning is unknown.

### Data pages

`READ_DATA_PAGE` (`0x22`) takes the page number as its single parameter:

| Page | Response type | Contents |
|---|---|---|
| `0x01` | `SerialNumberResponse` | ASCII serial, UTF-16-ish (every other byte) |
| `0x02` | `P0TablesResponse` | Three 512×424 `uint16` phase-offset tables |
| `0x03` | `DepthCameraParamsResponse` | IR intrinsics and distortion |
| `0x04` | `RgbCameraParamsResponse` | Color intrinsics and the depth→color polynomial |

`DepthCameraParamsResponse` is the source of `getIrCameraParams()`: `fx`, `fy`,
`cx`, `cy` plus `k1`, `k2`, `k3` radial and `p1`, `p2` tangential terms. The
tangential terms have only ever been observed as zero, and the field mapping is
an educated guess checked against Kinect SDK calibration data. `unknown0` and a
trailing `unknown1[13]` are assumed to be zero.

`RgbCameraParamsResponse` supplies `getColorCameraParams()`: a single focal
length `color_f` used for both axes, the principal point, `shift_d`/`shift_m`,
and two ten-term cubic polynomials (`mx_*`, `my_*`) that map depth coordinates
into the color image. It also carries two float tables of 28×23 — an aspect
ratio suspiciously close to 512×424 — believed related to the `xtable`/`ztable`
used by the deconvolution code. libfreenect2 does not currently use them.

The P0 tables are the largest read at up to `0x1C0000` bytes. Each of the three
tables is 512×424 `uint16` values bracketed by unknown 16-bit fields, with known
constant row values (`0x2c9a`, `0x08ec`, `0x42e8`) that serve as a sanity check.
They feed `loadP0TablesFromCommandResponse()` on the depth packet processor.

## Open, start, and stop sequences

### Open

```
setConfiguration
claimInterfaces                  (unless the caller supplied its own)
setIsochronousDelay
setIrInterfaceState(Disabled)    alt setting 0
enablePowerStates
setVideoTransferFunctionState(Disabled)
getIrMaxIsoPacketSize            must be >= 0x8400
allocate transfer pools
```

`setPowerStateLatencies()` exists but its call site is commented out.

### Start

```
setVideoTransferFunctionState(Enabled)
READ_FIRMWARE_VERSIONS
READ_HARDWARE_INFO
READ_DATA_PAGE 0x01              serial number
READ_DATA_PAGE 0x03              depth camera params
READ_DATA_PAGE 0x02              P0 tables
READ_DATA_PAGE 0x04              rgb camera params
SET_MODE enabled, 0x00640064
SET_MODE disabled
poll READ_STATUS 0x090000        until bit 0 is set, or timeout
INIT_STREAMS
setIrInterfaceState(Enabled)     alt setting 1
READ_STATUS 0x090000
SET_STREAMING enabled
submit rgb and depth transfers
```

The `0x090000` status poll is the device telling the host it is ready. On a
cold start it can take several seconds; a device restarted immediately after a
previous run typically reports not-ready for the first few attempts. That is
normal and is the reason `Protonect` sometimes needs a second launch.

The trailing `SET_MODE` / `READ_DATA_0x26` / `READ_STATUS 0x100007` block in
`Freenect2DeviceImpl::start()` is a transcription of what the Microsoft driver
does. Its purpose is not understood, and the two `0x46`/`0x47` commands
alongside it are commented out.

### Stop

```
cancel rgb and depth transfers
setIrInterfaceState(Disabled)
SET_MODE enabled 0x00640064 / SET_MODE disabled
STOP
SET_STREAMING disabled
SET_MODE enabled / disabled     (twice more)
setVideoTransferFunctionState(Disabled)
```

The repeated mode toggles mirror captured traffic; they are not known to be
individually necessary.

## Color stream framing

Color arrives on bulk endpoint `0x83` as JPEG wrapped in a header and footer
(`src/rgb_packet_stream_parser.cpp`):

```c
struct RawRgbPacket {
  uint32_t sequence;
  uint32_t magic_header;   // 'BBBB' == 0x42424242
  unsigned char jpeg_buffer[];
};
```

After the JPEG's `FFD9` end-of-image marker come up to three `0xa5` alignment
bytes, then a run of `'Z'` filler, then:

```c
struct RgbPacketFooter {
  uint32_t magic_header;   // '9999' == 0x39393939
  uint32_t sequence;
  uint32_t filler_length;
  uint32_t unknown1;       // always 0 so far
  uint32_t unknown2;       // always 0 so far
  uint32_t timestamp;
  float    exposure;       // ~0.5 to ~60
  float    gain;           // ~1.0 clear to ~1.5 covered
  uint32_t magic_footer;   // 'BBBB' == 0x42424242
  uint32_t packet_size;
  float    gamma;          // ~1.0 to ~6.4 when covered
  uint32_t unknown4[3];    // always 0 so far
};
```

A packet is accepted only when both footer magics match, `packet_size` equals
the received length, and the header and footer sequence numbers agree. The
`exposure`, `gain`, and `gamma` interpretations are inferred from how the values
move when the lens is covered, not from documentation.

## Depth stream framing

Depth arrives on isochronous endpoint `0x84`. One depth frame is **ten
subframes**, each a 512×424 image of 11-bit packed samples:

```
298496 bytes per subframe = 512 * 424 * 11 / 8
```

Each subframe ends with a footer
(`include/internal/libfreenect2/depth_packet_stream_parser.h`):

```c
struct DepthSubPacketFooter {
  uint32_t magic0;
  uint32_t magic1;
  uint32_t timestamp;
  uint32_t sequence;
  uint32_t subsequence;   // 0..9
  uint32_t length;
  uint32_t fields[32];
};
```

The parser accumulates subframes into a ten-image buffer, tracking arrivals in a
bitmask. A frame is complete when the mask reaches `0x3ff` — all ten bits — and
is dropped if the sequence number changes before that. A `subsequence` of 10 or
more is rejected as invalid.

Only nine of the ten subframes reach the depth math. The tenth is decoded in
neither the CPU processor nor its GPU ports: the call is present but commented
out in `src/cpu_depth_packet_processor.cpp` (`processPixelStage2`, the block
labelled `10th measurement`), and upstream's USB analysis independently notes
that the tenth sub-layer is not used for depth computation. What the device
intends it for is unknown.

### Timing

Each isochronous service interval is 125 µs. The transfer pattern repeats every
266–267 packets, which is `266.6 × 125 µs ≈ 33.33 ms` — exactly the 30 Hz frame
period. The nine used subframes are transmitted in under 100 packets at the
start of each period; the remainder of the period is idle. Transfers must still
be submitted during the idle stretch, because an endpoint with no queued URB
loses its bandwidth reservation.

This timing analysis is upstream's, from the libfreenect2 wiki's Linux USB
notes; the packet-count arithmetic is reproduced here because it explains the
transfer pool sizing described in @ref linux_usb.

## Color camera settings

`RGB_SETTING` (`0x3E`) carries four parameters matching Microsoft's
`NUISENSOR_RGB_CHANGE_STREAM_SETTING` structure, and replies with a
`ColorSettingResponse` of `NumStatus`, `CommandListStatus`, `Status`, and
`Data`. libfreenect2 sends one setting per command.

The setting IDs are enumerated in `include/libfreenect2/color_settings.h`
(`ColorSettingCommandType`, values 0–83: exposure mode, integration time, white
balance mode, per-channel gains, analog and digital gain, exposure
compensation, metering mode and zone weights, frame rate, and more). Sequence
numbers are always zero for these commands.

Applications should prefer the wrappers — `setColorAutoExposure()`,
`setColorSemiAutoExposure()`, `setColorManualExposure()` — over raw settings.
See @ref configuration for the supported combinations.

## LED settings

`LED_SETTING` (`0x4B`) takes a `LedSettings` payload
(`include/libfreenect2/led_settings.h`), whose original Microsoft struct name
was `_PETRA_LED_STATE`:

| Field | Meaning |
|---|---|
| `LedId` | Which LED, 0 or 1 |
| `Mode` | 0 constant, 1 blink between the two levels |
| `StartLevel` | Intensity 0–1000 |
| `StopLevel` | Second intensity for blink mode |
| `IntervalInMs` | Blink period |

Exposed as `Freenect2Device::setLedStatus()`.

## What is still unknown

Worth stating plainly, so nobody assumes this page is complete:

* The purpose of commands `0x26`, `0x46`, `0x47`, and the `SET_MODE` parameter
  values `0x00640064` and `0x00500050`.
* Status addresses other than `0x090000`. `0x100007` is read and discarded;
  `0x02006F` and `0x020070` are declared but never sent.
* The IR normalization tables the hardware version would select, which the
  Microsoft SDK has and this library does not.
* The interrupt endpoint `0x82`.
* The tenth depth subframe.
* `magic0`/`magic1` in the depth footer and the 32 trailing `fields`.
* The two 28×23 float tables in the color parameters response.
* Firmware update, which is why this fork does not implement it.

## Next steps

* @ref linux_usb &mdash; how the framing above translates into bandwidth and
  transfer pool sizing.
* @ref configuration &mdash; the public knobs built on these commands.
* @ref kinect_v1_vs_v2 &mdash; why v1 protocol documentation does not apply.
