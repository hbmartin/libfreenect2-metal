# Using libfreenect2 from Python {#python}

Python bindings are a perennial request
([#280](https://github.com/OpenKinect/libfreenect2/issues/280)) and most
Python questions boil down to two recipes: getting raw depth values
([#1100](https://github.com/OpenKinect/libfreenect2/issues/1100)) and
mapping between RGB and depth
([#1049](https://github.com/OpenKinect/libfreenect2/issues/1049)).

## Bindings

libfreenect2 itself ships no Python bindings. Two community packages wrap
it:

* [pylibfreenect2](https://github.com/r9y9/pylibfreenect2) (Cython) — the
  most widely used. Build libfreenect2 first, then
  `pip install pylibfreenect2` with `LIBFREENECT2_INSTALL_PREFIX` pointing
  at your install prefix.
* [freenect2-python](https://github.com/rjw57/freenect2-python) (cffi) —
  uses `pkg-config` to find `freenect2.pc` from your install.

### Apple Silicon notes

* Build and install this fork first
  (`cmake -DCMAKE_INSTALL_PREFIX=$HOME/freenect2 .. && make install`), then
  build the binding against it. Everything (Python interpreter, binding,
  libfreenect2, libusb) must be **arm64**; a Rosetta/x86_64 Python cannot
  load an arm64 `libfreenect2.dylib`. `python3 -c "import platform;
  print(platform.machine())"` should print `arm64`.
* If the binding cannot find the library at runtime, set
  `DYLD_FALLBACK_LIBRARY_PATH=$HOME/freenect2/lib`.
* Select the GPU pipeline with the `LIBFREENECT2_PIPELINE=metal`
  environment variable if the binding predates the Metal pipeline names.

## Raw depth values

With pylibfreenect2, the depth frame is already the raw measurement: a
512x424 `float32` array in millimeters (0 = invalid), not a colorized
visualization.

```python
frames = listener.waitForNewFrame()
depth = frames["depth"].asarray()      # (424, 512) float32, mm
distance_at_center = depth[212, 256]
listener.release(frames)
```

If you saw scaled/8-bit values you were reading a *rendered* image (e.g.
from OpenCV normalization), not the frame data.

## Mapping RGB onto depth (and depth onto RGB)

Use `Registration`, exactly like the C++ API:

```python
from pylibfreenect2 import Registration, Frame

registration = Registration(device.getIrCameraParams(),
                            device.getColorCameraParams())
undistorted = Frame(512, 424, 4)
registered = Frame(512, 424, 4)
bigdepth = Frame(1920, 1082, 4)   # optional: depth for every color pixel

registration.apply(color, depth, undistorted, registered, bigdepth=bigdepth)

rgb_at_depth_pixels = registered.asarray(np.uint8)   # (424, 512, 4)
depth_at_color_pixels = bigdepth.asarray(np.float32)[1:-1, :]  # (1080, 1920)
x, y, z = registration.getPointXYZ(undistorted, r, c)  # meters
```

`registered` gives you the color value for every depth pixel; `bigdepth`
gives you the depth value for every color pixel (rows 0 and 1081 are
padding). See [registration.md](registration.md) for the underlying
semantics.
