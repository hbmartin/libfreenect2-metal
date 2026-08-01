#!/usr/bin/env python3
"""Run clang-tidy once per first-party translation unit in a CMake database."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("--source-dir", type=Path, default=Path.cwd())
    parser.add_argument(
        "--clang-tidy", default=os.environ.get("CLANG_TIDY", "clang-tidy")
    )
    parser.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    parser.add_argument(
        "--timeout", type=int, default=300, help="seconds allowed per source file"
    )
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    build_dir = args.build_dir.resolve()
    source_dir = args.source_dir.resolve()
    database = json.loads((build_dir / "compile_commands.json").read_text())
    roots = tuple(
        (source_dir / name).resolve() for name in ("src", "tests", "examples", "tools")
    )

    files: set[Path] = set()
    for entry in database:
        source = Path(entry["file"])
        if not source.is_absolute():
            source = Path(entry["directory"]) / source
        source = source.resolve()
        if source.suffix in {".cpp", ".cc", ".cxx", ".mm"} and any(
            source.is_relative_to(root) for root in roots
        ):
            files.add(source)

    if not files:
        raise SystemExit(
            "no first-party translation units found in compile_commands.json"
        )

    def analyze(source: Path) -> tuple[Path, int, str]:
        try:
            process = subprocess.run(
                [args.clang_tidy, "-p", str(build_dir), str(source)],
                cwd=source_dir,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
                timeout=args.timeout,
            )
        except subprocess.TimeoutExpired as error:
            output = error.stdout or ""
            if isinstance(output, bytes):
                output = output.decode(errors="replace")
            return (
                source,
                1,
                output + f"clang-tidy timed out after {args.timeout} seconds\n",
            )
        return source, process.returncode, process.stdout

    failed = False
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        for source, returncode, output in executor.map(analyze, sorted(files)):
            if output.strip():
                print(f"===== {source.relative_to(source_dir)} =====")
                print(output, end="" if output.endswith("\n") else "\n")
            failed = failed or returncode != 0

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
