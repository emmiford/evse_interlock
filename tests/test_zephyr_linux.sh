#!/usr/bin/env bash
set -euo pipefail
SCRIPT_NAME="$(basename "$0")"
trap 'echo "FAIL: ${SCRIPT_NAME}" >&2' ERR

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BOARD="${BOARD:-native_posix}"
SIDEWALK_PATCH="${SIDEWALK_PATCH:-$ROOT_DIR/app/evse_interlock_v1/patches/sidewalk-ble-off.patch}"

if [[ -f "${SIDEWALK_PATCH}" ]]; then
  if git -C "${ROOT_DIR}/sidewalk" apply --reverse --check "${SIDEWALK_PATCH}" >/dev/null 2>&1; then
    echo "INFO: sidewalk patch already applied"
  else
    echo "INFO: applying sidewalk patch: ${SIDEWALK_PATCH}"
    git -C "${ROOT_DIR}/sidewalk" apply "${SIDEWALK_PATCH}"
  fi
fi

if [[ -n "${TESTS:-}" ]]; then
  IFS=" " read -r -a TESTS <<< "${TESTS}"
else
  TESTS=("telemetry/gpio_event" "telemetry/telemetry" "safety_gate")
fi

for test_name in "${TESTS[@]}"; do
  TEST_DIR="${ROOT_DIR}/app/evse_interlock_v1/tests/${test_name}"
  BUILD_DIR="${ROOT_DIR}/build-tests/${test_name}"
  APP_NAME="$(basename "${TEST_DIR}")"

  west build -p always -b "${BOARD}" -d "${BUILD_DIR}" "${TEST_DIR}"
  if [[ -f "${BUILD_DIR}/${APP_NAME}/build.ninja" ]]; then
    west build -t run -d "${BUILD_DIR}/${APP_NAME}"
  else
    west build -t run -d "${BUILD_DIR}"
  fi
done

echo "PASS: ${SCRIPT_NAME}"
