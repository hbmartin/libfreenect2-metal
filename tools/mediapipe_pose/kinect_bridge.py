"""ctypes wrapper for the demo-only native Kinect bridge."""

from __future__ import annotations

import ctypes
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

import numpy as np


class _StreamInfo(ctypes.Structure):
    _fields_ = [
        ("color_width", ctypes.c_uint32),
        ("color_height", ctypes.c_uint32),
        ("depth_width", ctypes.c_uint32),
        ("depth_height", ctypes.c_uint32),
        ("rgb_buffer_size", ctypes.c_size_t),
        ("pipeline", ctypes.c_int),
    ]


class _FrameInfo(ctypes.Structure):
    _fields_ = [
        ("color_timestamp", ctypes.c_uint32),
        ("depth_timestamp", ctypes.c_uint32),
        ("color_sequence", ctypes.c_uint32),
        ("depth_sequence", ctypes.c_uint32),
        ("synchronization_delta_ms", ctypes.c_float),
        ("synchronization_valid", ctypes.c_uint8),
    ]


@dataclass(frozen=True)
class FrameInfo:
    color_timestamp: int
    depth_timestamp: int
    color_sequence: int
    depth_sequence: int
    synchronization_delta_ms: float
    synchronization_valid: bool


def _default_library_candidates() -> list[Path]:
    repository = Path(__file__).resolve().parents[2]
    if sys.platform == "darwin":
        names = ("libmediapipe_kinect_bridge.dylib",)
    elif sys.platform == "win32":
        names = ("mediapipe_kinect_bridge.dll", "libmediapipe_kinect_bridge.dll")
    else:
        names = ("libmediapipe_kinect_bridge.so",)
    return [repository / "build" / "lib" / name for name in names]


def find_bridge(explicit_path: Optional[str]) -> Path:
    candidates = [Path(explicit_path).expanduser()] if explicit_path else _default_library_candidates()
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    rendered = ", ".join(str(path) for path in candidates)
    raise FileNotFoundError(
        f"MediaPipe Kinect bridge not found ({rendered}). "
        "Configure with -DBUILD_MEDIAPIPE_DEMO=ON and build mediapipe_kinect_bridge."
    )


class KinectCapture:
    STATUS_OK = 0
    STATUS_TIMEOUT = 1
    PIPELINES = {0: "CPU", 1: "Metal"}

    def __init__(self, library_path: Path, serial: str = "", pipeline: str = "auto"):
        self._library = ctypes.CDLL(str(library_path))
        self._configure_api()
        self._handle = ctypes.c_void_p()
        stream = _StreamInfo()
        error = ctypes.create_string_buffer(1024)
        status = self._library.mp_pose_capture_open(
            serial.encode("utf-8"),
            pipeline.encode("ascii"),
            ctypes.byref(self._handle),
            ctypes.byref(stream),
            error,
            len(error),
        )
        if status != self.STATUS_OK:
            raise RuntimeError(error.value.decode("utf-8", errors="replace"))
        self.width = int(stream.color_width)
        self.height = int(stream.color_height)
        self.depth_width = int(stream.depth_width)
        self.depth_height = int(stream.depth_height)
        self.pipeline = self.PIPELINES.get(int(stream.pipeline), f"unknown({stream.pipeline})")
        self.rgb = np.empty((self.height, self.width, 3), dtype=np.uint8)
        if self.rgb.nbytes != stream.rgb_buffer_size:
            self.close()
            raise RuntimeError("native bridge and NumPy disagree about RGB buffer size")

    def _configure_api(self) -> None:
        library = self._library
        library.mp_pose_capture_open.argtypes = [
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_void_p),
            ctypes.POINTER(_StreamInfo),
            ctypes.c_char_p,
            ctypes.c_size_t,
        ]
        library.mp_pose_capture_open.restype = ctypes.c_int
        library.mp_pose_capture_next.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_ubyte),
            ctypes.c_size_t,
            ctypes.c_int,
            ctypes.POINTER(_FrameInfo),
        ]
        library.mp_pose_capture_next.restype = ctypes.c_int
        library.mp_pose_capture_lift.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_float),
            ctypes.c_size_t,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_float,
            ctypes.POINTER(ctypes.c_float),
            ctypes.POINTER(ctypes.c_uint8),
        ]
        library.mp_pose_capture_lift.restype = ctypes.c_int
        library.mp_pose_capture_last_error.argtypes = [ctypes.c_void_p]
        library.mp_pose_capture_last_error.restype = ctypes.c_char_p
        library.mp_pose_capture_close.argtypes = [ctypes.c_void_p]
        library.mp_pose_capture_close.restype = None

    def _last_error(self) -> str:
        value = self._library.mp_pose_capture_last_error(self._handle)
        return value.decode("utf-8", errors="replace") if value else "unknown bridge error"

    def next_frame(self, timeout_ms: int = 10_000) -> tuple[np.ndarray, FrameInfo]:
        native_info = _FrameInfo()
        status = self._library.mp_pose_capture_next(
            self._handle,
            self.rgb.ctypes.data_as(ctypes.POINTER(ctypes.c_ubyte)),
            self.rgb.nbytes,
            timeout_ms,
            ctypes.byref(native_info),
        )
        if status == self.STATUS_TIMEOUT:
            raise TimeoutError(self._last_error())
        if status != self.STATUS_OK:
            raise RuntimeError(self._last_error())
        return self.rgb, FrameInfo(
            color_timestamp=int(native_info.color_timestamp),
            depth_timestamp=int(native_info.depth_timestamp),
            color_sequence=int(native_info.color_sequence),
            depth_sequence=int(native_info.depth_sequence),
            synchronization_delta_ms=float(native_info.synchronization_delta_ms),
            synchronization_valid=bool(native_info.synchronization_valid),
        )

    def lift(
        self,
        normalized_xy: np.ndarray,
        primary_radius: int = 8,
        fallback_radius: int = 20,
        cluster_span_mm: float = 150.0,
    ) -> tuple[np.ndarray, np.ndarray]:
        xy = np.ascontiguousarray(normalized_xy, dtype=np.float32)
        if xy.ndim != 2 or xy.shape[1] != 2:
            raise ValueError("normalized_xy must have shape (N, 2)")
        xyz = np.empty((xy.shape[0], 3), dtype=np.float32)
        valid = np.empty(xy.shape[0], dtype=np.uint8)
        status = self._library.mp_pose_capture_lift(
            self._handle,
            xy.ctypes.data_as(ctypes.POINTER(ctypes.c_float)),
            xy.shape[0],
            primary_radius,
            fallback_radius,
            cluster_span_mm,
            xyz.ctypes.data_as(ctypes.POINTER(ctypes.c_float)),
            valid.ctypes.data_as(ctypes.POINTER(ctypes.c_uint8)),
        )
        if status != self.STATUS_OK:
            raise RuntimeError(self._last_error())
        return xyz, valid.astype(bool)

    def close(self) -> None:
        if getattr(self, "_handle", None) and self._handle.value:
            self._library.mp_pose_capture_close(self._handle)
            self._handle = ctypes.c_void_p()

    def __enter__(self) -> "KinectCapture":
        return self

    def __exit__(self, *_args: object) -> None:
        self.close()

    def __del__(self) -> None:
        self.close()
