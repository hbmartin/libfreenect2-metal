#!/usr/bin/env python3
"""Download and verify the pinned MediaPipe Full pose model."""

from __future__ import annotations

import argparse
import hashlib
import os
import tempfile
import urllib.request
from pathlib import Path

MODEL_URL = (
    "https://storage.googleapis.com/mediapipe-models/pose_landmarker/"
    "pose_landmarker_full/float16/latest/pose_landmarker_full.task"
)
MODEL_SHA256 = "4eaa5eb7a98365221087693fcc286334cf0858e2eb6e15b506aa4a7ecdcec4ad"
DEFAULT_DESTINATION = Path(__file__).resolve().parent / "models" / "pose_landmarker_full.task"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as model:
        for block in iter(lambda: model.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def download(destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.is_file() and sha256(destination) == MODEL_SHA256:
        print(f"Model already verified: {destination}")
        return

    descriptor, temporary_name = tempfile.mkstemp(
        prefix=destination.name + ".", suffix=".tmp", dir=destination.parent
    )
    os.close(descriptor)
    temporary = Path(temporary_name)
    try:
        print(f"Downloading {MODEL_URL}")
        with urllib.request.urlopen(MODEL_URL, timeout=60) as response, temporary.open("wb") as output:
            while block := response.read(1024 * 1024):
                output.write(block)
        actual = sha256(temporary)
        if actual != MODEL_SHA256:
            raise RuntimeError(f"model checksum mismatch: expected {MODEL_SHA256}, got {actual}")
        temporary.replace(destination)
        print(f"Verified model: {destination}")
    finally:
        temporary.unlink(missing_ok=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, default=DEFAULT_DESTINATION)
    arguments = parser.parse_args()
    download(arguments.destination.expanduser().resolve())


if __name__ == "__main__":
    main()
