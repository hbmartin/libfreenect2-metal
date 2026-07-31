"""Hardware-free pose smoothing, angle, and recording helpers."""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Any, Iterable, Optional, Sequence

Point3 = tuple[float, float, float]
OptionalPoint3 = Optional[Point3]

LANDMARK_NAMES = (
    "nose",
    "left_eye_inner",
    "left_eye",
    "left_eye_outer",
    "right_eye_inner",
    "right_eye",
    "right_eye_outer",
    "left_ear",
    "right_ear",
    "mouth_left",
    "mouth_right",
    "left_shoulder",
    "right_shoulder",
    "left_elbow",
    "right_elbow",
    "left_wrist",
    "right_wrist",
    "left_pinky",
    "right_pinky",
    "left_index",
    "right_index",
    "left_thumb",
    "right_thumb",
    "left_hip",
    "right_hip",
    "left_knee",
    "right_knee",
    "left_ankle",
    "right_ankle",
    "left_heel",
    "right_heel",
    "left_foot_index",
    "right_foot_index",
)

LANDMARK_INDEX = {name: index for index, name in enumerate(LANDMARK_NAMES)}

POSE_CONNECTIONS = (
    (0, 1), (1, 2), (2, 3), (3, 7),
    (0, 4), (4, 5), (5, 6), (6, 8), (9, 10),
    (11, 12), (11, 13), (13, 15), (15, 17), (17, 19),
    (19, 15), (15, 21), (12, 14), (14, 16), (16, 18),
    (18, 20), (20, 16), (16, 22), (11, 23), (12, 24),
    (23, 24), (23, 25), (25, 27), (27, 29), (29, 31),
    (31, 27), (24, 26), (26, 28), (28, 30), (30, 32),
    (32, 28),
)

ANGLE_SPECS = {
    "left_elbow": ("left_shoulder", "left_elbow", "left_wrist"),
    "right_elbow": ("right_shoulder", "right_elbow", "right_wrist"),
    "left_shoulder": ("left_elbow", "left_shoulder", "left_hip"),
    "right_shoulder": ("right_elbow", "right_shoulder", "right_hip"),
    "left_hip": ("left_shoulder", "left_hip", "left_knee"),
    "right_hip": ("right_shoulder", "right_hip", "right_knee"),
    "left_knee": ("left_hip", "left_knee", "left_ankle"),
    "right_knee": ("right_hip", "right_knee", "right_ankle"),
    "left_ankle": ("left_knee", "left_ankle", "left_foot_index"),
    "right_ankle": ("right_knee", "right_ankle", "right_foot_index"),
}


def _finite_point(point: Sequence[float]) -> bool:
    return len(point) == 3 and all(math.isfinite(value) for value in point)


def angle_degrees(first: Point3, vertex: Point3, third: Point3) -> Optional[float]:
    """Return the smaller 3D angle at vertex, or None for a degenerate vector."""
    left = tuple(first[i] - vertex[i] for i in range(3))
    right = tuple(third[i] - vertex[i] for i in range(3))
    left_length = math.sqrt(sum(value * value for value in left))
    right_length = math.sqrt(sum(value * value for value in right))
    if left_length <= 1e-8 or right_length <= 1e-8:
        return None
    cosine = sum(left[i] * right[i] for i in range(3)) / (left_length * right_length)
    return math.degrees(math.acos(max(-1.0, min(1.0, cosine))))


def midpoint(first: Point3, second: Point3) -> Point3:
    return tuple((first[i] + second[i]) * 0.5 for i in range(3))  # type: ignore[return-value]


class EmaLandmarks:
    """Per-landmark EMA that forgets state after a fixed missing-frame run."""

    def __init__(self, count: int, alpha: float = 0.35, reset_after: int = 5):
        if count <= 0 or not 0.0 < alpha <= 1.0 or reset_after <= 0:
            raise ValueError("invalid EMA configuration")
        self.alpha = alpha
        self.reset_after = reset_after
        self._points: list[OptionalPoint3] = [None] * count
        self._missing = [0] * count

    def update(self, points: Sequence[OptionalPoint3]) -> list[OptionalPoint3]:
        if len(points) != len(self._points):
            raise ValueError("landmark count changed")
        for index, point in enumerate(points):
            if point is None or not _finite_point(point):
                self._missing[index] += 1
                if self._missing[index] >= self.reset_after:
                    self._points[index] = None
                continue
            value = (float(point[0]), float(point[1]), float(point[2]))
            previous = self._points[index]
            if previous is None:
                self._points[index] = value
            else:
                self._points[index] = tuple(
                    self.alpha * value[axis] + (1.0 - self.alpha) * previous[axis]
                    for axis in range(3)
                )  # type: ignore[assignment]
            self._missing[index] = 0
        return list(self._points)


@dataclass(frozen=True)
class Measurement:
    value: Optional[float]
    source: str

    def to_dict(self) -> dict[str, Any]:
        return {
            "value_degrees": None if self.value is None else round(self.value, 3),
            "source": self.source,
        }


def _measurement_from_triplet(
    names: tuple[str, str, str],
    kinect: Sequence[OptionalPoint3],
    model: Sequence[OptionalPoint3],
) -> Measurement:
    indices = tuple(LANDMARK_INDEX[name] for name in names)
    kinect_points = tuple(kinect[index] for index in indices)
    if all(point is not None for point in kinect_points):
        value = angle_degrees(*kinect_points)  # type: ignore[arg-type]
        if value is not None:
            return Measurement(value, "kinect")
    model_points = tuple(model[index] for index in indices)
    if all(point is not None for point in model_points):
        value = angle_degrees(*model_points)  # type: ignore[arg-type]
        if value is not None:
            return Measurement(value, "model")
    return Measurement(None, "unavailable")


def _trunk_measurement(
    kinect: Sequence[OptionalPoint3], model: Sequence[OptionalPoint3]
) -> Measurement:
    indices = tuple(
        LANDMARK_INDEX[name]
        for name in ("left_shoulder", "right_shoulder", "left_hip", "right_hip")
    )
    for points, source in ((kinect, "kinect"), (model, "model")):
        selected = tuple(points[index] for index in indices)
        if not all(point is not None for point in selected):
            continue
        shoulder = midpoint(selected[0], selected[1])  # type: ignore[arg-type]
        hip = midpoint(selected[2], selected[3])  # type: ignore[arg-type]
        # Both Kinect camera coordinates and MediaPipe pose world coordinates
        # use negative Y for camera-up.
        reference = (hip[0], hip[1] - 1.0, hip[2])
        value = angle_degrees(shoulder, hip, reference)
        if value is not None:
            return Measurement(value, source)
    return Measurement(None, "unavailable")


def compute_measurements(
    kinect: Sequence[OptionalPoint3], model: Sequence[OptionalPoint3]
) -> dict[str, Measurement]:
    if len(kinect) != len(LANDMARK_NAMES) or len(model) != len(LANDMARK_NAMES):
        raise ValueError("expected 33 MediaPipe landmarks")
    result = {
        name: _measurement_from_triplet(spec, kinect, model)
        for name, spec in ANGLE_SPECS.items()
    }
    result["trunk_inclination"] = _trunk_measurement(kinect, model)
    return result


def build_record(
    *,
    wall_time_utc: str,
    frame: dict[str, Any],
    landmarks: Iterable[dict[str, Any]],
    measurements: dict[str, Measurement],
    fps: float,
    pose_detected: bool,
) -> dict[str, Any]:
    return {
        "schema_version": 1,
        "wall_time_utc": wall_time_utc,
        "frame": frame,
        "pose_detected": pose_detected,
        "landmarks": list(landmarks),
        "measurements": {name: value.to_dict() for name, value in measurements.items()},
        "processing_fps": round(fps, 3),
    }
