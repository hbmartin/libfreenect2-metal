# USB bandwidth and transfer tuning {#linux_usb}

Advanced reference for how libfreenect2 moves data off the sensor, why the
Kinect v2 is unusually demanding of a USB 3.0 controller, and when the four
transfer-pool environment variables are the right tool.

For a symptom-first walkthrough, start with @ref troubleshooting instead. Most
of this page is Linux-specific, but the transfer-pool section applies to every
platform.

## Why the Kinect v2 needs a controller mostly to itself

The sensor streams two things at once:

* **Color**, as JPEG over a **bulk** endpoint.
* **IR/depth**, as raw phase data over an **isochronous** endpoint.

The IR stream is the demanding one. Depth is reconstructed from ten raw
subframes per depth frame at 512×424, delivered at 30 Hz, and the isochronous
endpoint reserves bandwidth up front whether or not it is used. Combined with
color, a single Kinect v2 consumes most of a 5 Gbps USB 3.0 link — real-world
throughput on a SuperSpeed link is well under the nominal 5 Gbps once encoding
and protocol overhead are accounted for.

The practical consequences:

* **One Kinect per host controller** is the safe rule. Two sensors sharing a
  controller usually means dropped transfers and stalled streams.
* **Hubs make it worse**, not better. A hub multiplexes onto one upstream link,
  so the sensors still share the same budget and add latency.
* **USB 2.0 is not merely slow, it is unsupported.** The isochronous bandwidth
  the IR endpoint requires does not exist at 480 Mbps.
* **PCIe expansion cards need lanes.** An x1 slot does not have the bandwidth
  for a USB3 controller running a Kinect; use x8 or x16.

### Confirming the link actually negotiated SuperSpeed

A Kinect plugged into a USB 3.0 port can still fall back to 480 Mbps on a bad
cable or a marginal port. On Linux, check the advertised speed in the topology:

```sh
lsusb -t
```

The Kinect's port must show `5000M`. Anything lower (`480M`, `12M`) means the
link did not come up at SuperSpeed and depth streaming will not work — replace
the cable, try a different port, and confirm you are using the Kinect's own
powered adapter.

On macOS the equivalent is:

```sh
system_profiler SPUSBDataType
```

Look for `Xbox NUI Sensor` with `Speed: Up to 5 Gb/s`.

### Identifying the controller

```sh
lspci -nn | grep -i usb
```

Include this in bug reports — the numeric `[vendor:device]` ID identifies the
controller far better than a brand name.

## Transfer pools

libfreenect2 allocates two pools of libusb transfers when the device is opened,
sized differently per platform. The library logs the result at `Info` level
every time it opens a device:

```
[Info] [Freenect2DeviceImpl] transfer pool sizes rgb: 20*16384 ir: 4*128*33792
```

Read as `rgb: <transfers>*<bytes>` and `ir: <transfers>*<packets>*<packet bytes>`.
The IR packet size is not a constant — it is read from the device's isochronous
endpoint descriptor at open time and must be at least `0x8400` (33792) bytes, or
the open fails.

### Defaults by platform

| Platform | `RGB_TRANSFERS` | `RGB_TRANSFER_SIZE` | `IR_TRANSFERS` | `IR_PACKETS` |
|---|---|---|---|---|
| Linux (and any other platform) | 20 | 0x4000 (16384) | 60 | 8 |
| macOS | 20 | 0x4000 (16384) | 4 | 128 |
| Windows | 3 | 1048576 (1 MiB) | 8 | 64 |

macOS uses few, large isochronous transfers; Linux uses many small ones. Windows
uses very few, very large bulk transfers because `poll()` there has a 64 file
descriptor limit, which a multi-Kinect setup would otherwise exhaust.

Total IR pool bytes are `IR_TRANSFERS × IR_PACKETS × packet size`. On macOS with
a 33792-byte packet that is `4 × 128 × 33792` ≈ 17.3 MB; the Linux default
`60 × 8 × 33792` works out to roughly the same total, split across many more
in-flight transfers.

### The four tuning variables

| Variable | Tunes |
|---|---|
| `LIBFREENECT2_RGB_TRANSFERS` | Number of bulk transfers in the color pool |
| `LIBFREENECT2_RGB_TRANSFER_SIZE` | Bytes per color transfer |
| `LIBFREENECT2_IR_TRANSFERS` | Number of isochronous transfers in the depth pool |
| `LIBFREENECT2_IR_PACKETS` | Isochronous packets per depth transfer |

Each overrides its platform default at device open. Verify an override took
effect by reading the log line back:

```sh
LIBFREENECT2_IR_PACKETS=64 LIBFREENECT2_IR_TRANSFERS=8 Protonect -noviewer -frames 5
# [Info] [Freenect2DeviceImpl] transfer pool sizes rgb: 20*16384 ir: 8*64*33792
```

### When tuning is appropriate

Reach for these only after ruling out topology, cabling, and link speed — those
cause far more failures than pool sizing does, and no pool size compensates for
two sensors on one controller.

* **More, smaller IR transfers** (raise `IR_TRANSFERS`, lower `IR_PACKETS`) give
  the kernel more in-flight requests and can help on a host that is failing to
  resubmit quickly enough under load.
* **Fewer, larger transfers** reduce per-transfer overhead and file-descriptor
  pressure — the reason for the Windows defaults.
* **Raising the total pool size** adds buffering against scheduling jitter at
  the cost of memory and latency.

Keep the product of transfers and packets in the same ballpark as the default
unless you are deliberately trading memory for jitter tolerance, and change one
variable at a time.

### USBFS memory limit (Linux only)

The kernel caps how much memory a process may pin for USB transfers. The default
is often too low for even one Kinect, and definitely too low for several. Check
and raise the current limit:

```sh
cat /sys/module/usbcore/parameters/usbfs_memory_mb
echo 1000 | sudo tee /sys/module/usbcore/parameters/usbfs_memory_mb
```

That change does not survive a reboot. To persist it, add
`usbcore.usbfs_memory_mb=1000` to the kernel command line, or set
`options usbcore usbfs_memory_mb=1000` in a file under `/etc/modprobe.d/`.

Symptoms of an insufficient limit are transfer submission failures at start,
often reported as `-ENOMEM` in `dmesg`. This limit does not exist on macOS or
Windows.

## Autosuspend

Linux may suspend an idle USB device, which the Kinect does not tolerate well
mid-session. Scope any change to the sensor rather than disabling autosuspend
globally — the blanket `echo -1` over every device in `/sys/bus/usb/devices/*`
that circulates online also affects keyboards, storage, and anything else
attached.

Find the Kinect's device path and disable autosuspend for that node only:

```sh
for d in /sys/bus/usb/devices/*; do
  [ -e "$d/idVendor" ] || continue
  if [ "$(cat "$d/idVendor")" = "045e" ]; then
    echo "$d $(cat "$d/idProduct")"
  fi
done
# then, for the matching device (PID 02c4 or 02d8):
echo on | sudo tee /sys/bus/usb/devices/<device>/power/control
```

To reverse it, write `auto` back to the same file, or simply replug the sensor —
the setting does not persist across reconnects unless a udev rule applies it.

## What not to change casually

Two suggestions that circulate for Kinect v2 USB problems have system-wide
effects well beyond this driver, and should be a last resort with a recorded
reversal step:

* **Kernel boot parameters** for the xHCI driver alter USB behavior for every
  device on the machine and can leave a system without input devices if wrong.
* **`vm.min_free_kbytes`** changes the kernel's memory reserve globally. Raising
  it can reduce allocation failures under memory pressure, but it also
  permanently withholds that memory from everything else on the system.

Prefer fixing topology, link speed, and the usbfs limit first. If you do apply
either of the above, note it in any bug report — it changes the baseline your
results are measured against.

## Next steps

* @ref troubleshooting &mdash; symptom-first diagnosis.
* @ref configuration &mdash; the full environment-variable reference.
* @ref recording_replay &mdash; running multiple sensors.
