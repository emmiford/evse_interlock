#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_DIR="${ROOT_DIR}/app/sidewalk_end_device/tests/gpio_event"
BUILD_DIR="${ROOT_DIR}/build-tests/gpio_event"
BOARD="${BOARD:-native_posix}"

west build -p always -b "${BOARD}" -d "${BUILD_DIR}" "${TEST_DIR}"
west build -t run -d "${BUILD_DIR}"
