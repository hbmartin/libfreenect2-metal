# Using libfreenect2 from Python {#python}

The maintained Python interface for this fork is
[pylibfreenect3](https://github.com/hbmartin/pylibfreenect3). It binds the
0.3 device, recording, alignment, registration, and vision APIs without a
ctypes bridge.

Its examples include an aligned OpenCV viewer and a complete MediaPipe pose
demo with reusable registration buffers and metric landmark lifting. OpenCV
and MediaPipe remain example-only dependencies.

The legacy native MediaPipe demo previously hosted in this repository has
moved there so capture, registration, and Python ownership semantics have one
canonical implementation.
