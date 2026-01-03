#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-tests/host"
SRC_DIR="${ROOT_DIR}/app/sidewalk_end_device"

mkdir -p "${BUILD_DIR}"

cc -std=c11 -Wall -Wextra -I"${SRC_DIR}/include" -I"${SRC_DIR}/src" \
  "${SRC_DIR}/src/gpio_event.c" \
  "${SRC_DIR}/src/telemetry/telemetry_gpio.c" \
  "${SRC_DIR}/src/telemetry/telemetry_evse.c" \
  "${SRC_DIR}/tests/host/main.c" \
  -o "${BUILD_DIR}/host_tests"

"${BUILD_DIR}/host_tests"
