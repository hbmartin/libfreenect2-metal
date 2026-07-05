# Runtime configuration reference

## Depth processing configuration (`Freenect2Device::Config`)

Set via `dev->setConfiguration(config)` **before** `start()`:

| Field | Default | Meaning |
|---|---|---|
| `MinDepth` | 0.5 m | measurements closer than this are invalidated |
| `MaxDepth` | 4.5 m | measurements farther than this are invalidated |
| `EnableBilateralFilter` | true | joint bilateral filter; removes "flying pixels" |
| `EnableEdgeAwareFilter` | true | suppresses noisy pixels on depth edges |

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
  useful when you do your own filtering.

## Environment variables

| Variable | Effect |
|---|---|
| `LIBFREENECT2_PIPELINE` | select pipeline: `gl`, `cuda`, `cl`, `metal`, `cpu` (falls back to the default order if unavailable) |
| `LIBFREENECT2_LOGGER_LEVEL` | `debug`, `info`, `warning`, `error`, or `none` |
| `LIBFREENECT2_TJ_FAST` | `1` enables TurboJPEG fast DCT/upsampling for RGB decode |
| `LIBFREENECT2_RGB_TRANSFERS` / `LIBFREENECT2_RGB_TRANSFER_SIZE` | tune the RGB bulk transfer pool |
| `LIBFREENECT2_IR_TRANSFERS` / `LIBFREENECT2_IR_PACKETS` | tune the depth isochronous transfer pool |
| `LIBUSB_DEBUG` | `3` for verbose libusb diagnostics of USB problems |

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
