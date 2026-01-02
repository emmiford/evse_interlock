#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="/Users/jan/dev/sidewalk-workspace"
BUILD_DIR="${ROOT_DIR}/build"
APP_DIR="${ROOT_DIR}/sidewalk/samples/sid_end_device"
PROBE_ID="${PROBE_ID:-0700000100120036470000124e544634a5a5a5a597969908}"
LOG_FILE="${ROOT_DIR}/build/hil_gpio_rtt.log"

west build -p always -d "${BUILD_DIR}" -b rak4631 "${APP_DIR}" -- \
  -DOVERLAY_CONFIG="overlay-hello.conf;overlay-gpio-test.conf" \
  -DPM_STATIC_YML_FILE:FILEPATH="${APP_DIR}/pm_static_rak4631_nrf52840.yml"

west flash --runner pyocd --build-dir "${BUILD_DIR}" -- \
  --target nrf52840 --dev-id "${PROBE_ID}"

python3 "${ROOT_DIR}/tools/test_hil_gpio.py" --probe "${PROBE_ID}" --timeout 40 --outfile "${LOG_FILE}"
