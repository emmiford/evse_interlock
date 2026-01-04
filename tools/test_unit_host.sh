#!/usr/bin/env bash
set -euo pipefail
SCRIPT_NAME="$(basename "$0")"
trap 'echo "FAIL: ${SCRIPT_NAME}" >&2' ERR

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-tests/host"
SRC_DIR="${ROOT_DIR}/app/sidewalk_end_device"

mkdir -p "${BUILD_DIR}"

cc -std=c11 -Wall -Wextra -I"${SRC_DIR}/include" -I"${SRC_DIR}/src" \
  "${SRC_DIR}/src/gpio_event.c" \
  "${SRC_DIR}/src/safety_gate.c" \
  "${SRC_DIR}/src/telemetry/telemetry_gpio.c" \
  "${SRC_DIR}/src/telemetry/telemetry_evse.c" \
  "${SRC_DIR}/tests/host/main.c" \
  "${SRC_DIR}/tests/host/telemetry_tests.c" \
  -o "${BUILD_DIR}/host_tests"

"${BUILD_DIR}/host_tests"
echo "PASS: ${SCRIPT_NAME}"
