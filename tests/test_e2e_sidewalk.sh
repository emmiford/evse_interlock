#!/usr/bin/env bash
set -euo pipefail
SCRIPT_NAME="$(basename "$0")"
trap 'echo "FAIL: ${SCRIPT_NAME}" >&2' ERR

ROOT_DIR="/Users/jan/dev/sidewalk-workspace"
BUILD_DIR="${ROOT_DIR}/build"
APP_DIR="${ROOT_DIR}/app/evse_interlock_v1"
PROBE_ID="${PROBE_ID:-0700000100120036470000124e544634a5a5a5a597969908}"
RTT_LOG="${ROOT_DIR}/build/e2e_rtt.log"
PAYLOAD_JSON="${ROOT_DIR}/build/e2e_payload.json"
REGION="${AWS_REGION:-us-east-1}"
PROJECT_PREFIX="${PROJECT_PREFIX:-sidewalk-v1}"
E2E_OVERLAY_CONFIG="${E2E_OVERLAY_CONFIG:-config/overlays/overlay-sidewalk_logging_v1.conf}"

if pgrep -f "pyocd rtt" >/dev/null 2>&1; then
  echo "INFO: stopping existing pyocd rtt before flashing"
  pkill -f "pyocd rtt" || true
  sleep 1
  if pgrep -f "pyocd rtt" >/dev/null 2>&1; then
    echo "FAIL: pyocd rtt is still running; stop it and retry" >&2
    exit 1
  fi
fi

west build -p always -d "${BUILD_DIR}" -b rak4631 "${APP_DIR}" -- \
  -DOVERLAY_CONFIG="${E2E_OVERLAY_CONFIG}" \
  -DPM_STATIC_YML_FILE:FILEPATH="${APP_DIR}/config/config/pm_static_rak4631_nrf52840.yml" \
  -Dmcuboot_PM_STATIC_YML_FILE:FILEPATH="${APP_DIR}/config/config/pm_static_rak4631_nrf52840.yml"

west flash --runner pyocd --build-dir "${BUILD_DIR}" -- \
  --target nrf52840 --dev-id "${PROBE_ID}"

RUN_ID="$(python3 "${ROOT_DIR}/tests/capture_rtt_run_id.py" --probe "${PROBE_ID}" --timeout 40 --logfile "${RTT_LOG}")"
echo "run_id=${RUN_ID}"

python3 "${ROOT_DIR}/tests/mqtt_wait_for_run_id.py" --run-id "${RUN_ID}" --timeout 90 --outfile "${PAYLOAD_JSON}"

json_get() {
  python3 -c 'import json,sys; data=json.load(open(sys.argv[1], "r", encoding="utf-8")); print(data.get(sys.argv[2], ""))' \
    "$1" "$2"
}

DEVICE_ID="$(json_get "${PAYLOAD_JSON}" "device_id")"
EVENT_ID="$(json_get "${PAYLOAD_JSON}" "event_id")"
TIMESTAMP_MS="$(json_get "${PAYLOAD_JSON}" "timestamp")"

if [[ -z "${DEVICE_ID}" ]]; then
  echo "FAIL: device_id not found in payload" >&2
  exit 1
fi
if [[ -z "${EVENT_ID}" ]]; then
  echo "FAIL: event_id not found in payload" >&2
  exit 1
fi
if [[ -z "${TIMESTAMP_MS}" ]]; then
  echo "FAIL: timestamp not found in payload" >&2
  exit 1
fi

python3 "${ROOT_DIR}/tests/e2e_verify_dynamodb.py" \
  --device-id "${DEVICE_ID}" \
  --run-id "${RUN_ID}" \
  --event-id "${EVENT_ID}" \
  --since-ms "$((TIMESTAMP_MS - 600000))" \
  --region "${REGION}" \
  --table "${PROJECT_PREFIX}-device_events_v2"
echo "PASS: ${SCRIPT_NAME}"
