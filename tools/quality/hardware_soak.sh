#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 3 ]]; then
  echo "usage: $0 PROTONECT ITERATIONS FRAMES [PIPELINE ...]" >&2
  exit 2
fi

protonect=$1
iterations=$2
frames=$3
shift 3

if [[ ! "$iterations" =~ ^[1-9][0-9]*$ || ! "$frames" =~ ^[1-9][0-9]*$ ]]; then
  echo "iterations and frames must be positive integers" >&2
  exit 2
fi

if [[ ! -x "$protonect" ]]; then
  echo "Protonect executable not found: $protonect" >&2
  exit 2
fi

if [[ $# -eq 0 ]]; then
  set -- cpu
fi

for pipeline in "$@"; do
  for ((iteration = 1; iteration <= iterations; ++iteration)); do
    echo "soak pipeline=$pipeline iteration=$iteration/$iterations frames=$frames"
    "$protonect" "$pipeline" -noviewer -frames "$frames"
  done
done
