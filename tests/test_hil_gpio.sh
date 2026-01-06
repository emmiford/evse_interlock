#!/usr/bin/env bash
set -euo pipefail
SCRIPT_NAME="$(basename "$0")"
trap 'echo "FAIL: ${SCRIPT_NAME}" >&2' ERR

ROOT_DIR="/Users/jan/dev/sidewalk-workspace"
BUILD_DIR="${ROOT_DIR}/build"
APP_DIR="${ROOT_DIR}/app/evse_interlock_v1"
PROBE_ID="${PROBE_ID:-0700000100120036470000124e544634a5a5a5a597969908}"
LOG_FILE="${ROOT_DIR}/build/hil_gpio_rtt.log"
HIL_MODE="${HIL_MODE:-basic}"
EXPECTED_TRANSITIONS="${EXPECTED_TRANSITIONS:-6}"
SIDEWALK_PATCH="${SIDEWALK_PATCH:-$APP_DIR/patches/sidewalk-ble-off.patch}"
BOARD_OVERLAY="${BOARD_OVERLAY:-config/overlays/rak4631.overlay}"
EXTRA_ARGS=()

if [[ "${HIL_MODE}" == "signal" ]]; then
  EXTRA_ARGS+=(--mode signal --expected-transitions "${EXPECTED_TRANSITIONS}")
fi
if [[ "${HIL_MODE}" == "safety" ]]; then
  EXTRA_ARGS+=(--mode safety --require-ac-asserted)
fi

if [[ -f "${SIDEWALK_PATCH}" ]]; then
  if git -C "${ROOT_DIR}/sidewalk" apply --reverse --check "${SIDEWALK_PATCH}" >/dev/null 2>&1; then
    echo "INFO: sidewalk patch already applied"
  else
    echo "INFO: applying sidewalk patch: ${SIDEWALK_PATCH}"
    git -C "${ROOT_DIR}/sidewalk" apply "${SIDEWALK_PATCH}"
  fi
fi

west build -p always -d "${BUILD_DIR}" -b rak4631 "${APP_DIR}" -- \
  -DOVERLAY_CONFIG="config/overlays/overlay-sidewalk_logging_v1.conf" \
  -DDTC_OVERLAY_FILE="${BOARD_OVERLAY}" \
  -DPM_STATIC_YML_FILE:FILEPATH="${APP_DIR}/config/config/pm_static_rak4631_nrf52840.yml" \
  -Dmcuboot_PM_STATIC_YML_FILE:FILEPATH="${APP_DIR}/config/config/pm_static_rak4631_nrf52840.yml"

west flash --runner pyocd --build-dir "${BUILD_DIR}" -- \
  --target nrf52840 --dev-id "${PROBE_ID}"

python3 "${ROOT_DIR}/tests/test_hil_gpio.py" --probe "${PROBE_ID}" --timeout 40 --outfile "${LOG_FILE}" "${EXTRA_ARGS[@]}"
echo "PASS: ${SCRIPT_NAME}"
