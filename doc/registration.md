# Registration and coordinate mapping recipes {#registration}

The `Registration` class answers every "how do I map between color, depth,
and 3D coordinates" question. This page collects the recipes behind the most
frequent upstream reports:
[#1113](https://github.com/OpenKinect/libfreenect2/issues/1113),
[#1072](https://github.com/OpenKinect/libfreenect2/issues/1072),
[#1086](https://github.com/OpenKinect/libfreenect2/issues/1086),
[#1095](https://github.com/OpenKinect/libfreenect2/issues/1095),
[#1062](https://github.com/OpenKinect/libfreenect2/issues/1062),
[#1068](https://github.com/OpenKinect/libfreenect2/issues/1068),
[#508](https://github.com/OpenKinect/libfreenect2/issues/508),
[#515](https://github.com/OpenKinect/libfreenect2/issues/515).

## Setup

```cpp
libfreenect2::Registration registration(dev->getIrCameraParams(),
                                        dev->getColorCameraParams());
libfreenect2::Frame undistorted(512, 424, 4), registered(512, 424, 4);
libfreenect2::Frame bigdepth(1920, 1082, 4);       // optional
int color_depth_map[512 * 424];                    // optional

registration.apply(rgb, depth, &undistorted, &registered,
                   /*enable_filter=*/true, &bigdepth, color_depth_map);
```

One `apply()` call produces every mapping at once:

| Output | Size | Meaning |
|---|---|---|
| `undistorted` | 512x424 float | depth image with lens distortion removed (mm) |
| `registered` | 512x424 BGRX | the color value for each depth pixel |
| `bigdepth` | 1920x**1082** float | the depth value for each color pixel; rows 0 and 1081 are padding, so use rows 1..1080 |
| `color_depth_map` | 512x424 int | index of the color pixel for each depth pixel (-1 if none) |

## Depth pixel -> color pixel (512x424 -> 1920x1080)

Either read `color_depth_map[r * 512 + c]` (an index into the 1920x1080
color image), or call `registration.apply(cx, cy, dz, ...)` for a single
point. This is the answer to "what color-space coordinate corresponds to
depth pixel (x, y)" (#1113, #1049).

## Color pixel -> depth value (1920x1080 -> mm)

Read `bigdepth` at `(row + 1, col)`. Pixels with no depth measurement are
`inf` (#1072 — check with `std::isfinite`, not `== 0`).

## 3D points and point clouds (#515, #1062)

Always use the **undistorted** frame:

```cpp
float x, y, z;        // meters, right-handed, camera at origin
registration.getPointXYZ(&undistorted, r, c, x, y, z);

float rgb;            // packed BGRX, PCL-style
registration.getPointXYZRGB(&undistorted, &registered, r, c, x, y, z, rgb);
```

Loop over all `(r, c)` and skip non-finite `z` to build a point cloud; the
result drops directly into `pcl::PointXYZRGB`-style containers.

Do not run `getPointXYZ` on the raw depth frame or on `registered` after
cropping — the pinhole model in it assumes the full 512x424 undistorted
geometry (#1095: offline registration of *cropped* images is not supported;
crop after mapping instead).

## Why the registered image loses the background (#1086)

`registered` only has color where there is a valid depth measurement — it
is "color resampled onto depth", not a composite. If you want the full color
image with depth where available, use the original `rgb` frame plus
`bigdepth`, which covers every color pixel.

## Offline / recorded data (#1095, #1068)

`Registration` is pure math over the two parameter structs; it does not
need a device. Save `getIrCameraParams()`/`getColorCameraParams()` with
your recordings, then reconstruct:

```cpp
libfreenect2::Freenect2Device::IrCameraParams ir = loadIr(...);
libfreenect2::Freenect2Device::ColorCameraParams color = loadColor(...);
libfreenect2::Registration registration(ir, color);
```

The `registered` output is aligned to the IR camera's geometry, so IR-based
calibration (e.g. of the registered image) uses the IR intrinsics (#1068).

## Projection matrices (#508)

libfreenect2 does not expose ready-made 4x4 matrices, but both parameter
structs are public plain data, so the standard matrices are one-liners:

```
K_ir  = [[fx, 0, cx], [0, fy, cy], [0, 0, 1]]        from IrCameraParams
K_rgb = [[fx, 0, cx], [0, fy, cy], [0, 0, 1]]        from ColorCameraParams
```

Distortion (`k1 k2 k3 p1 p2`) applies to the raw IR image only (see
[depth_accuracy.md](depth_accuracy.md)). The color camera's
`shift_d/shift_m/m_x/m_y` members encode the depth-to-color mapping used
internally by `apply()`; they are not a conventional extrinsic matrix. If
you need a true extrinsic calibration between the cameras, calibrate
externally.
