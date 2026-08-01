#!/usr/bin/env python3
"""Fail when an llvm-cov summary falls below the project's ratcheting floor."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("report", type=Path)
    parser.add_argument("--min-lines", type=float, default=30.0)
    parser.add_argument("--min-branches", type=float, default=25.0)
    args = parser.parse_args()

    total_line = next(
        (
            line
            for line in args.report.read_text().splitlines()
            if line.startswith("TOTAL")
        ),
        None,
    )
    if total_line is None:
        raise SystemExit("coverage report has no TOTAL row")

    percentages = [
        float(value) for value in re.findall(r"([0-9]+(?:\.[0-9]+)?)%", total_line)
    ]
    if len(percentages) != 4:
        raise SystemExit(f"unexpected llvm-cov TOTAL row: {total_line}")

    _, _, line_coverage, branch_coverage = percentages
    print(f"line coverage: {line_coverage:.2f}% (minimum {args.min_lines:.2f}%)")
    print(f"branch coverage: {branch_coverage:.2f}% (minimum {args.min_branches:.2f}%)")

    failures: list[str] = []
    if line_coverage < args.min_lines:
        failures.append("line coverage is below the required floor")
    if branch_coverage < args.min_branches:
        failures.append("branch coverage is below the required floor")
    if failures:
        raise SystemExit("; ".join(failures))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
