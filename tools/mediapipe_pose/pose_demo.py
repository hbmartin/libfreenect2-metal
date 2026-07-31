#!/usr/bin/env python3
"""Live MediaPipe exercise pose with Kinect-derived metric landmarks."""

from __future__ import annotations

import argparse
import json
import math
import sys
import time
from collections import deque
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Optional, Sequence

from kinect_bridge import FrameInfo, KinectCapture, find_bridge
from pose_math import (
    EmaLandmarks,
    LANDMARK_NAMES,
    POSE_CONNECTIONS,
    Measurement,
    OptionalPoint3,
    build_record,
    compute_measurements,
)

DEMO_DIRECTORY = Path(__file__).resolve().parent
DEFAULT_MODEL = DEMO_DIRECTORY / "models" / "pose_landmarker_full.task"
WINDOW_NAME = "MediaPipe Kinect Exercise Pose"
GREEN = (70, 220, 80)
ORANGE = (40, 165, 255)
GRAY = (130, 130, 130)
WHITE = (235, 235, 235)
CYAN = (230, 210, 60)


class FpsCounter:
    def __init__(self, window: int = 60):
        self._samples: deque[float] = deque(maxlen=window)

    def tick(self) -> float:
        self._samples.append(time.monotonic())
        if len(self._samples) < 2:
            return 0.0
        elapsed = self._samples[-1] - self._samples[0]
        return (len(self._samples) - 1) / elapsed if elapsed > 0.0 else 0.0


class SessionRecorder:
    def __init__(self, output: Path):
        self.output = output
        self._stream: Optional[Any] = None

    @property
    def active(self) -> bool:
        return self._stream is not None

    def start(self) -> None:
        if self.active:
            return
        self.output.mkdir(parents=True, exist_ok=True)
        self._stream = (self.output / "session.jsonl").open("a", encoding="utf-8")

    def stop(self) -> None:
        if self._stream:
            self._stream.close()
            self._stream = None

    def toggle(self) -> None:
        self.stop() if self.active else self.start()

    def write(self, record: dict[str, Any]) -> None:
        if not self._stream:
            return
        self._stream.write(json.dumps(record, allow_nan=False, separators=(",", ":")) + "\n")
        self._stream.flush()

    def snapshot(self, image: Any, record: dict[str, Any], cv2: Any) -> tuple[Path, Path]:
        self.output.mkdir(parents=True, exist_ok=True)
        sequence = record["frame"]["color_sequence"]
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
        stem = f"frame_{sequence}_{stamp}"
        image_path = self.output / f"{stem}.png"
        json_path = self.output / f"{stem}.json"
        if not cv2.imwrite(str(image_path), image):
            raise RuntimeError(f"Unable to write snapshot {image_path}")
        with json_path.open("w", encoding="utf-8") as output:
            json.dump(record, output, allow_nan=False, indent=2)
            output.write("\n")
        return image_path, json_path


def _optional_point(values: Sequence[float], enabled: bool) -> OptionalPoint3:
    if not enabled or len(values) != 3 or not all(math.isfinite(float(value)) for value in values):
        return None
    return (float(values[0]), float(values[1]), float(values[2]))


def _serializable_point(point: OptionalPoint3) -> Optional[list[float]]:
    return None if point is None else [round(float(value), 6) for value in point]


def _empty_measurements() -> dict[str, Measurement]:
    empty: list[OptionalPoint3] = [None] * len(LANDMARK_NAMES)
    return compute_measurements(empty, empty)


def _frame_record(frame: FrameInfo, pipeline: str) -> dict[str, Any]:
    return {
        "color_timestamp_ticks": frame.color_timestamp,
        "depth_timestamp_ticks": frame.depth_timestamp,
        "color_sequence": frame.color_sequence,
        "depth_sequence": frame.depth_sequence,
        "synchronization_delta_ms": round(frame.synchronization_delta_ms, 3),
        "synchronization_valid": frame.synchronization_valid,
        "pipeline": pipeline,
    }


def _landmark_records(
    image_landmarks: Sequence[Any],
    world_landmarks: Sequence[Any],
    kinect_points: Sequence[OptionalPoint3],
) -> list[dict[str, Any]]:
    records = []
    for index, name in enumerate(LANDMARK_NAMES):
        image = image_landmarks[index]
        world = world_landmarks[index]
        records.append(
            {
                "name": name,
                "image_normalized": [
                    round(float(image.x), 6),
                    round(float(image.y), 6),
                    round(float(image.z), 6),
                ],
                "visibility": round(float(image.visibility), 6),
                "presence": round(float(getattr(image, "presence", 1.0)), 6),
                "model_world_m": [
                    round(float(world.x), 6),
                    round(float(world.y), 6),
                    round(float(world.z), 6),
                ],
                "kinect_xyz_m": _serializable_point(kinect_points[index]),
            }
        )
    return records


def _point_color(index: int, measured: Sequence[bool], confident: Sequence[bool]) -> tuple[int, int, int]:
    if not confident[index]:
        return GRAY
    return GREEN if measured[index] else ORANGE


def _draw_rgb_pose(
    rgb: Any,
    image_landmarks: Sequence[Any],
    measured: Sequence[bool],
    confident: Sequence[bool],
    cv2: Any,
) -> Any:
    view = cv2.cvtColor(rgb, cv2.COLOR_RGB2BGR)
    view = cv2.resize(view, (960, 540), interpolation=cv2.INTER_AREA)
    points = [
        (int(round(landmark.x * (view.shape[1] - 1))), int(round(landmark.y * (view.shape[0] - 1))))
        for landmark in image_landmarks
    ]
    for first, second in POSE_CONNECTIONS:
        color = _point_color(first, measured, confident)
        if _point_color(second, measured, confident) != color:
            color = GRAY if not (confident[first] and confident[second]) else ORANGE
        cv2.line(view, points[first], points[second], color, 2, cv2.LINE_AA)
    for index, point in enumerate(points):
        cv2.circle(view, point, 4, _point_color(index, measured, confident), -1, cv2.LINE_AA)
    return view


def _project(point: tuple[float, float, float], mode: str, box: tuple[int, int, int, int]) -> tuple[int, int]:
    x0, y0, width, height = box
    if mode == "front":
        horizontal = (point[0] + 1.5) / 3.0
    else:
        horizontal = (point[2] - 0.5) / 4.0
    vertical = (point[1] + 1.5) / 3.0
    return (
        x0 + int(max(0.0, min(1.0, horizontal)) * width),
        y0 + int(max(0.0, min(1.0, vertical)) * height),
    )


def _draw_3d_view(panel: Any, points: Sequence[OptionalPoint3], cv2: Any) -> None:
    boxes = ((15, 40, 245, 260, "front", "Front: X / Y"), (295, 40, 245, 260, "side", "Side: Z / Y"))
    for x, y, width, height, mode, title in boxes:
        cv2.rectangle(panel, (x, y), (x + width, y + height), (70, 70, 70), 1)
        cv2.putText(panel, title, (x, y - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.5, WHITE, 1, cv2.LINE_AA)
        box = (x, y, width, height)
        for first, second in POSE_CONNECTIONS:
            if points[first] is None or points[second] is None:
                continue
            cv2.line(panel, _project(points[first], mode, box), _project(points[second], mode, box), GREEN, 2, cv2.LINE_AA)
        for point in points:
            if point is not None:
                cv2.circle(panel, _project(point, mode, box), 3, GREEN, -1, cv2.LINE_AA)


def _render(
    rgb: Any,
    image_landmarks: Sequence[Any],
    kinect_points: Sequence[OptionalPoint3],
    currently_measured: Sequence[bool],
    confident: Sequence[bool],
    measurements: dict[str, Measurement],
    fps: float,
    frame: FrameInfo,
    pipeline: str,
    recording: bool,
    paused: bool,
    cv2: Any,
    np: Any,
) -> Any:
    main = _draw_rgb_pose(rgb, image_landmarks, currently_measured, confident, cv2) if image_landmarks else cv2.resize(
        cv2.cvtColor(rgb, cv2.COLOR_RGB2BGR), (960, 540), interpolation=cv2.INTER_AREA
    )
    canvas = np.zeros((720, 1520, 3), dtype=np.uint8)
    canvas[:540, :960] = main
    panel = canvas[:, 960:]
    _draw_3d_view(panel, kinect_points, cv2)

    sync_color = GREEN if frame.synchronization_valid else ORANGE
    cv2.putText(canvas, f"{pipeline}  {fps:4.1f} FPS", (18, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.75, WHITE, 2, cv2.LINE_AA)
    cv2.putText(
        canvas,
        f"RGB-depth {frame.synchronization_delta_ms:.1f} ms",
        (18, 60),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.65,
        sync_color,
        2,
        cv2.LINE_AA,
    )
    cv2.putText(canvas, f"Measured joints: {sum(currently_measured)}/33", (18, 90), cv2.FONT_HERSHEY_SIMPLEX, 0.65, GREEN, 2, cv2.LINE_AA)

    cv2.putText(panel, "Exercise measurements", (15, 340), cv2.FONT_HERSHEY_SIMPLEX, 0.65, WHITE, 2, cv2.LINE_AA)
    order = (
        "left_elbow", "right_elbow", "left_shoulder", "right_shoulder",
        "left_hip", "right_hip", "left_knee", "right_knee",
        "left_ankle", "right_ankle", "trunk_inclination",
    )
    for row, name in enumerate(order):
        measurement = measurements[name]
        value = "--" if measurement.value is None else f"{measurement.value:5.1f} deg"
        source = "" if measurement.source == "unavailable" else f" [{measurement.source}]"
        cv2.putText(
            panel,
            f"{name.replace('_', ' '):<19} {value}{source}",
            (15, 370 + row * 27),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.47,
            GREEN if measurement.source == "kinect" else ORANGE if measurement.source == "model" else GRAY,
            1,
            cv2.LINE_AA,
        )

    state = "PAUSED" if paused else "RECORDING" if recording else "LIVE"
    state_color = CYAN if paused else (40, 40, 240) if recording else GREEN
    cv2.putText(canvas, state, (18, 585), cv2.FONT_HERSHEY_SIMPLEX, 0.8, state_color, 2, cv2.LINE_AA)
    cv2.putText(canvas, "Q/Esc quit   Space pause   R record JSONL   S snapshot", (18, 630), cv2.FONT_HERSHEY_SIMPLEX, 0.65, WHITE, 1, cv2.LINE_AA)
    cv2.putText(canvas, "Green = Kinect metric XYZ   Orange = model-only   Gray = low confidence", (18, 670), cv2.FONT_HERSHEY_SIMPLEX, 0.57, GRAY, 1, cv2.LINE_AA)
    return canvas


def _parse_arguments() -> argparse.Namespace:
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bridge", help="path to libmediapipe_kinect_bridge")
    parser.add_argument("--model", type=Path, default=DEFAULT_MODEL, help="MediaPipe .task model")
    parser.add_argument("--serial", default="", help="Kinect serial; defaults to the first device")
    parser.add_argument("--pipeline", choices=("auto", "metal", "cpu"), default="auto")
    parser.add_argument("--output", type=Path, default=Path("captures") / f"mediapipe_pose_{timestamp}")
    parser.add_argument("--visibility", type=float, default=0.6)
    parser.add_argument("--presence", type=float, default=0.6)
    parser.add_argument("--depth-radius", type=int, default=8)
    parser.add_argument("--depth-fallback-radius", type=int, default=20)
    parser.add_argument("--cluster-span-mm", type=float, default=150.0)
    parser.add_argument("--timeout-ms", type=int, default=10_000)
    return parser.parse_args()


def main() -> int:
    arguments = _parse_arguments()
    model = arguments.model.expanduser().resolve()
    if not model.is_file():
        raise FileNotFoundError(f"Pose model not found: {model}. Run tools/mediapipe_pose/download_model.py")
    if not 0.0 <= arguments.visibility <= 1.0 or not 0.0 <= arguments.presence <= 1.0:
        raise ValueError("visibility and presence must be between zero and one")
    if arguments.depth_radius < 0:
        raise ValueError("depth radius must be non-negative")
    if arguments.depth_fallback_radius < arguments.depth_radius:
        raise ValueError("depth fallback radius must be at least the primary radius")
    if arguments.depth_fallback_radius > 1920:
        raise ValueError("depth fallback radius must not exceed the Kinect color-image dimensions")

    import cv2
    import mediapipe as mp
    import numpy as np

    bridge = find_bridge(arguments.bridge)
    recorder = SessionRecorder(arguments.output.expanduser().resolve())
    kinect_filter = EmaLandmarks(len(LANDMARK_NAMES), alpha=0.35, reset_after=5)
    model_filter = EmaLandmarks(len(LANDMARK_NAMES), alpha=0.35, reset_after=5)
    fps_counter = FpsCounter()
    started = time.monotonic_ns()
    last_timestamp_ms = -1
    last_canvas = None
    last_record = None
    paused = False
    timeout_count = 0

    options = mp.tasks.vision.PoseLandmarkerOptions(
        base_options=mp.tasks.BaseOptions(model_asset_path=str(model)),
        running_mode=mp.tasks.vision.RunningMode.VIDEO,
        num_poses=1,
        min_pose_detection_confidence=0.5,
        min_pose_presence_confidence=0.5,
        min_tracking_confidence=0.5,
        output_segmentation_masks=False,
    )

    cv2.namedWindow(WINDOW_NAME, cv2.WINDOW_NORMAL)
    cv2.resizeWindow(WINDOW_NAME, 1520, 720)
    try:
        with KinectCapture(bridge, arguments.serial, arguments.pipeline) as capture, \
                mp.tasks.vision.PoseLandmarker.create_from_options(options) as landmarker:
            while True:
                if paused and last_canvas is not None:
                    cv2.imshow(WINDOW_NAME, last_canvas)
                    key = cv2.waitKey(30) & 0xFF
                    if key in (ord("q"), 27):
                        break
                    if key == ord(" "):
                        paused = False
                    elif key == ord("r"):
                        recorder.toggle()
                    elif key == ord("s") and last_record is not None:
                        recorder.snapshot(last_canvas, last_record, cv2)
                    continue

                try:
                    rgb, frame = capture.next_frame(arguments.timeout_ms)
                    timeout_count = 0
                except TimeoutError:
                    timeout_count += 1
                    if timeout_count >= 3:
                        raise RuntimeError(
                            f"no frames received after {timeout_count} timeouts; "
                            "check the Kinect connection"
                        ) from None
                    continue

                timestamp_ms = max(last_timestamp_ms + 1, (time.monotonic_ns() - started) // 1_000_000)
                last_timestamp_ms = timestamp_ms
                result = landmarker.detect_for_video(
                    mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb), timestamp_ms
                )
                fps = fps_counter.tick()
                image_landmarks: Sequence[Any] = ()
                world_landmarks: Sequence[Any] = ()
                confident = [False] * len(LANDMARK_NAMES)
                raw_kinect: list[OptionalPoint3] = [None] * len(LANDMARK_NAMES)
                raw_model: list[OptionalPoint3] = [None] * len(LANDMARK_NAMES)

                if result.pose_landmarks:
                    image_landmarks = result.pose_landmarks[0]
                    world_landmarks = result.pose_world_landmarks[0]
                    confident = [
                        float(point.visibility) >= arguments.visibility
                        and float(getattr(point, "presence", 1.0)) >= arguments.presence
                        for point in image_landmarks
                    ]
                    coordinates = np.asarray([(point.x, point.y) for point in image_landmarks], dtype=np.float32)
                    xyz, depth_valid = capture.lift(
                        coordinates,
                        arguments.depth_radius,
                        arguments.depth_fallback_radius,
                        arguments.cluster_span_mm,
                    )
                    raw_kinect = [
                        _optional_point(xyz[index], bool(depth_valid[index]) and confident[index])
                        for index in range(len(LANDMARK_NAMES))
                    ]
                    raw_model = [
                        _optional_point(
                            (world_landmarks[index].x, world_landmarks[index].y, world_landmarks[index].z),
                            confident[index],
                        )
                        for index in range(len(LANDMARK_NAMES))
                    ]

                smoothed_kinect = kinect_filter.update(raw_kinect)
                smoothed_model = model_filter.update(raw_model)
                measurements = compute_measurements(smoothed_kinect, smoothed_model)
                landmark_records = (
                    _landmark_records(image_landmarks, world_landmarks, raw_kinect)
                    if image_landmarks else []
                )
                record = build_record(
                    wall_time_utc=datetime.now(timezone.utc).isoformat(),
                    frame=_frame_record(frame, capture.pipeline),
                    landmarks=landmark_records,
                    measurements=measurements,
                    fps=fps,
                    pose_detected=bool(image_landmarks),
                )
                canvas = _render(
                    rgb,
                    image_landmarks,
                    smoothed_kinect,
                    [point is not None for point in raw_kinect],
                    confident,
                    measurements,
                    fps,
                    frame,
                    capture.pipeline,
                    recorder.active,
                    False,
                    cv2,
                    np,
                )
                recorder.write(record)
                last_canvas = canvas
                last_record = record
                cv2.imshow(WINDOW_NAME, canvas)
                key = cv2.waitKey(1) & 0xFF
                if key in (ord("q"), 27) or cv2.getWindowProperty(WINDOW_NAME, cv2.WND_PROP_VISIBLE) < 1:
                    break
                if key == ord(" "):
                    paused = True
                    last_canvas = _render(
                        rgb, image_landmarks, smoothed_kinect,
                        [point is not None for point in raw_kinect], confident, measurements,
                        fps, frame, capture.pipeline, recorder.active, True, cv2, np
                    )
                elif key == ord("r"):
                    recorder.toggle()
                elif key == ord("s"):
                    recorder.snapshot(canvas, record, cv2)
    finally:
        recorder.stop()
        cv2.destroyAllWindows()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
    except RuntimeError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
