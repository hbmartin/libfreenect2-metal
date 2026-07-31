"""Hardware-free tests for the MediaPipe pose demo's pure Python logic."""

from __future__ import annotations

import json
import math
import sys
import unittest
from pathlib import Path

DEMO_DIRECTORY = Path(__file__).resolve().parents[1] / "tools" / "mediapipe_pose"
sys.path.insert(0, str(DEMO_DIRECTORY))

from pose_math import (  # noqa: E402
    EmaLandmarks,
    LANDMARK_INDEX,
    LANDMARK_NAMES,
    Measurement,
    angle_degrees,
    build_record,
    compute_measurements,
)


class AngleTests(unittest.TestCase):
    def test_right_and_straight_angles(self) -> None:
        self.assertAlmostEqual(angle_degrees((1, 0, 0), (0, 0, 0), (0, 1, 0)), 90.0)
        self.assertAlmostEqual(angle_degrees((-1, 0, 0), (0, 0, 0), (1, 0, 0)), 180.0)
        self.assertIsNone(angle_degrees((0, 0, 0), (0, 0, 0), (1, 0, 0)))

    def test_kinect_measurement_wins_without_mixing_sources(self) -> None:
        kinect = [None] * len(LANDMARK_NAMES)
        model = [None] * len(LANDMARK_NAMES)
        indices = [LANDMARK_INDEX[name] for name in ("left_shoulder", "left_elbow", "left_wrist")]
        for points in (kinect, model):
            points[indices[0]] = (1.0, 0.0, 0.0)
            points[indices[1]] = (0.0, 0.0, 0.0)
            points[indices[2]] = (0.0, 1.0, 0.0)

        measurements = compute_measurements(kinect, model)
        self.assertEqual(measurements["left_elbow"].source, "kinect")
        self.assertAlmostEqual(measurements["left_elbow"].value, 90.0)

        kinect[indices[2]] = None
        measurements = compute_measurements(kinect, model)
        self.assertEqual(measurements["left_elbow"].source, "model")
        self.assertAlmostEqual(measurements["left_elbow"].value, 90.0)

        model[indices[0]] = None
        measurements = compute_measurements(kinect, model)
        self.assertEqual(measurements["left_elbow"].source, "unavailable")
        self.assertIsNone(measurements["left_elbow"].value)

    def test_trunk_inclination_uses_camera_up_for_both_coordinate_streams(self) -> None:
        shoulders = (LANDMARK_INDEX["left_shoulder"], LANDMARK_INDEX["right_shoulder"])
        hips = (LANDMARK_INDEX["left_hip"], LANDMARK_INDEX["right_hip"])
        for source in ("kinect", "model"):
            kinect = [None] * len(LANDMARK_NAMES)
            model = [None] * len(LANDMARK_NAMES)
            points = kinect if source == "kinect" else model
            for index in shoulders:
                points[index] = (0.0, -1.0, 2.0)
            for index in hips:
                points[index] = (0.0, 0.0, 2.0)

            measurement = compute_measurements(kinect, model)["trunk_inclination"]
            self.assertEqual(measurement.source, source)
            self.assertAlmostEqual(measurement.value, 0.0)


class SmoothingTests(unittest.TestCase):
    def test_ema_smooths_and_resets_after_five_missing_frames(self) -> None:
        smoother = EmaLandmarks(1, alpha=0.35, reset_after=5)
        self.assertEqual(smoother.update([(1.0, 2.0, 3.0)])[0], (1.0, 2.0, 3.0))
        smoothed = smoother.update([(3.0, 2.0, 1.0)])[0]
        self.assertAlmostEqual(smoothed[0], 1.7)
        for _ in range(4):
            self.assertIsNotNone(smoother.update([None])[0])
        self.assertIsNone(smoother.update([None])[0])


class RecordingTests(unittest.TestCase):
    def test_record_is_strict_json_and_preserves_provenance(self) -> None:
        measurements = {
            "left_elbow": Measurement(90.0, "kinect"),
            "right_elbow": Measurement(None, "unavailable"),
        }
        record = build_record(
            wall_time_utc="2026-07-30T12:00:00+00:00",
            frame={"color_sequence": 10, "synchronization_valid": True},
            landmarks=[{"name": "nose", "kinect_xyz_m": [0.0, 0.0, 1.0]}],
            measurements=measurements,
            fps=29.97,
            pose_detected=True,
        )
        encoded = json.dumps(record, allow_nan=False)
        decoded = json.loads(encoded)
        self.assertEqual(decoded["schema_version"], 1)
        self.assertEqual(decoded["frame"]["color_sequence"], 10)
        self.assertTrue(decoded["frame"]["synchronization_valid"])
        self.assertEqual(decoded["landmarks"][0]["name"], "nose")
        self.assertEqual(decoded["landmarks"][0]["kinect_xyz_m"], [0.0, 0.0, 1.0])
        self.assertEqual(decoded["measurements"]["left_elbow"]["source"], "kinect")
        self.assertIsNone(decoded["measurements"]["right_elbow"]["value_degrees"])
        self.assertTrue(math.isclose(decoded["processing_fps"], 29.97))

    def test_non_finite_measurement_and_fps_remain_strict_json(self) -> None:
        record = build_record(
            wall_time_utc="2026-07-30T12:00:00+00:00",
            frame={"color_sequence": 1, "synchronization_valid": True},
            landmarks=[],
            measurements={"left_elbow": Measurement(float("nan"), "kinect")},
            fps=float("inf"),
            pose_detected=True,
        )
        decoded = json.loads(json.dumps(record, allow_nan=False))
        self.assertIsNone(decoded["measurements"]["left_elbow"]["value_degrees"])
        self.assertIsNone(decoded["processing_fps"])


if __name__ == "__main__":
    unittest.main()
