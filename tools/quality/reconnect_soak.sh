#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
  echo "usage: $0 KINECT_RECONNECT ITERATIONS [SERIAL]" >&2
  exit 2
fi

reconnect=$1
iterations=$2
serial=${3:-}

if [[ ! "$iterations" =~ ^[1-9][0-9]*$ ]]; then
  echo "iterations must be a positive integer" >&2
  exit 2
fi

if [[ ! -x "$reconnect" ]]; then
  echo "KinectReconnect executable not found: $reconnect" >&2
  exit 2
fi

for ((iteration = 1; iteration <= iterations; ++iteration)); do
  echo "reconnect soak iteration=$iteration/$iterations"
  echo "Watch the program output, unplug the Kinect, then reconnect it within 60 seconds."
  if [[ -n "$serial" ]]; then
    "$reconnect" "$serial"
  else
    "$reconnect"
  fi
done
