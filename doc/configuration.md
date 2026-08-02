# Runtime configuration reference {#configuration}

## Depth processing configuration (`Freenect2Device::Config`)

Set via `dev->setConfiguration(config)` **before** `start()`:

| Field | Default | Meaning |
|---|---|---|
| `MinDepth` | 0.5 m | measurements closer than this are invalidated |
| `MaxDepth` | 4.5 m | measurements farther than this are invalidated |
| `EnableBilateralFilter` | true | joint bilateral filter; removes "flying pixels" |
| `EnableEdgeAwareFilter` | true | suppresses noisy pixels on depth edges |

The two filter switches are independent in the CPU, OpenCL, CUDA, OpenGL, and
Metal depth processors:

| Bilateral | Edge-aware | Processing |
|---|---|---|
| on | on | default two-stage spatial filtering |
| on | off | bilateral smoothing only |
| off | on | edge-aware filtering of the un-smoothed phase result |
| off | off | no conventional spatial filtering |

When bilateral filtering is off, the backends provide an explicit all-valid
stage-1 edge mask. This makes edge-only output deterministic and prevents a
fresh or previously reconfigured processor from consuming unwritten GPU/CPU
memory. The edge-aware stage still applies its own depth-neighborhood and IR
consistency checks. The OpenCL/CUDA KDE pipelines retain their separate KDE
final-filter behavior.

Limits and behavior that were previously undocumented
([#163](https://github.com/OpenKinect/libfreenect2/issues/163)):

* The sensor's specified range is 0.5-4.5 m. `MaxDepth` can be raised —
  the phase unwrapping is unambiguous to about 18.75 m — but confidence
  drops sharply past ~8 m and the fixed unwrapping parameters produce
  increasing numbers of wrong-period outliers. Values beyond 18.75 m
  cannot work.
* The GPU pipelines (OpenCL, CUDA) bake `MinDepth`/`MaxDepth` into the
  compiled kernels; calling `setConfiguration` with new clip values on a
  running pipeline triggers a kernel rebuild. Configure before `start()`.
* The bilateral/edge filter kernels have a fixed 3x3 support; the filter
  strengths (`DepthPacketProcessor::Parameters`) are internal constants
  compiled into the processors and are not part of the public `Config`.
* Disabling both filters gives raw, noisier depth including flying pixels —
  useful when you do your own filtering. Edge-only mode is supported when you
  want discontinuity rejection without bilateral smoothing.

## Environment variables

| Variable | Effect |
|---|---|
| `LIBFREENECT2_PIPELINE` | select pipeline: `cpu`, `metal`, `opengl`, `opencl`, `opencl_kde`, `cuda`, `cuda_kde`, or `dump` (legacy `gl`/`cl` aliases are accepted here only; falls back to the default order if unavailable) |
| `LIBFREENECT2_RGB_PROCESSOR` | in automatic mode, select the RGB decoder: `auto`, `turbojpeg`, `videotoolbox`, `vaapi`, or `tegrajpeg` |
| `LIBFREENECT2_VAAPI_DEVICE` | in automatic mode, use one explicit VAAPI DRM node, such as `/dev/dri/renderD128` |
| `LIBFREENECT2_LOGGER_LEVEL` | `debug`, `info`, `warning`, `error`, or `none` |
| `LIBFREENECT2_TJ_FAST` | `1` enables TurboJPEG fast DCT/upsampling for RGB decode |
| `LIBFREENECT2_RGB_TRANSFERS` / `LIBFREENECT2_RGB_TRANSFER_SIZE` | override the RGB bulk transfer pool (see defaults below) |
| `LIBFREENECT2_IR_TRANSFERS` / `LIBFREENECT2_IR_PACKETS` | override the depth isochronous transfer pool (see defaults below) |
| `LIBUSB_DEBUG` | `3` for verbose libusb diagnostics of USB problems |

### Transfer pool defaults

The four transfer variables override platform-specific defaults chosen at device
open:

| Platform | `RGB_TRANSFERS` | `RGB_TRANSFER_SIZE` | `IR_TRANSFERS` | `IR_PACKETS` |
|---|---|---|---|---|
| Linux (and any other platform) | 20 | 0x4000 (16384) | 60 | 8 |
| macOS | 20 | 0x4000 (16384) | 4 | 128 |
| Windows | 3 | 1048576 (1 MiB) | 8 | 64 |

The isochronous packet size is not configurable — it is read from the device's
endpoint descriptor at open time and must be at least `0x8400` (33792) bytes.
The library logs the resolved pool sizes at `Info` level on every open, so an
override can be confirmed by reading the log back:

```
[Info] [Freenect2DeviceImpl] transfer pool sizes rgb: 20*16384 ir: 4*128*33792
```

Read as `rgb: <transfers>*<bytes>` and `ir: <transfers>*<packets>*<packet bytes>`.
See @ref linux_usb for what these control, the USB bandwidth budget behind them,
and when changing them is appropriate.

## RGB decoder selection (`PacketPipelineConfig`)

RGB decoding is configured independently for each packet pipeline. Pass the
configuration to a pipeline constructor or to the configured pipeline factory:

```cpp
libfreenect2::PacketPipelineConfig config;
config.rgb_decoder = libfreenect2::PacketPipelineConfig::VAAPI;
config.vaapi_device = "/dev/dri/renderD128";
config.allow_fallback = true;

libfreenect2::PacketPipeline *pipeline =
    libfreenect2::createDefaultPacketPipeline(config);
```

The decoder choices are `Auto`, `TurboJPEG`, `VideoToolbox`, `VAAPI`, and
`TegraJPEG`. A non-`Auto` choice in `PacketPipelineConfig` is authoritative:
the environment cannot replace either that decoder or its VAAPI device path.
With `Auto`, `LIBFREENECT2_RGB_PROCESSOR` selects a decoder when set, and
`LIBFREENECT2_VAAPI_DEVICE` supplies the VAAPI device when the configuration
does not already contain one. Invalid decoder names are ignored with a
warning.

Without an override, the compiled platform default is used: VideoToolbox on
Apple platforms, VAAPI on supported Linux builds, TegraJPEG on Tegra builds,
and TurboJPEG otherwise. Automatic VAAPI discovery probes DRM render nodes
before legacy card nodes, in lexical order, and accepts only nodes that expose
baseline JPEG VLD. NVIDIA nodes are skipped during automatic discovery because
probing them has caused driver failures; an explicitly configured NVIDIA path
is still attempted.

`allow_fallback` defaults to `true`. If VAAPI or TegraJPEG cannot initialize,
or if either fails while decoding a packet, that packet is retried once with
TurboJPEG and all later packets stay on TurboJPEG. The failed hardware-decoder
attempt never publishes an error or duplicate frame. With an explicit decoder
and fallback disabled, an initialization or runtime failure leaves the pipeline
unhealthy instead of claiming that a valid frame was produced.

For diagnostics, enable `LIBFREENECT2_LOGGER_LEVEL=info`. VAAPI reports the
chosen DRM node and driver. Selection failures, unsupported output formats,
incomplete TegraJPEG results, and the one-time switch to TurboJPEG are logged
as warnings or errors. Call `pipeline->good()` before opening a device when
strict selection is required.

## Color camera settings

`Freenect2Device` exposes the color camera's firmware controls:
`setColorAutoExposure()`, `setColorSemiAutoExposure()`,
`setColorManualExposure()`, and the low-level `setColorSetting()` /
`getColorSetting()` with the `COLOR_SETTING_*` command codes from
`color_settings.h`.

**What is ACS?**
([#1141](https://github.com/OpenKinect/libfreenect2/issues/1141)) —
`COLOR_SETTING_SET_ACS` (command 25) mirrors a setting observed in the
official SDK's USB traffic; its firmware semantics were never publicly
documented (it is commonly assumed to control the auto-exposure curve
selection). libfreenect2 sets it to 0 before changing exposure modes,
matching what the SDK does. Treat it as an opaque compatibility knob.
