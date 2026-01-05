#!/usr/bin/env bash
set -euo pipefail
SCRIPT_NAME="$(basename "$0")"
trap 'echo "FAIL: ${SCRIPT_NAME}" >&2' ERR

ROOT_DIR="/Users/jan/dev/sidewalk-workspace"
BUILD_DIR="${ROOT_DIR}/build"
APP_DIR="${ROOT_DIR}/app/sidewalk_end_device"
PROBE_ID="${PROBE_ID:-0700000100120036470000124e544634a5a5a5a597969908}"
RTT_LOG="${ROOT_DIR}/build/e2e_rtt.log"
PAYLOAD_JSON="${ROOT_DIR}/build/e2e_payload.json"
REGION="${AWS_REGION:-us-east-1}"
PROJECT_PREFIX="${PROJECT_PREFIX:-sidewalk-v1}"

west build -p always -d "${BUILD_DIR}" -b rak4631 "${APP_DIR}" -- \
  -DOVERLAY_CONFIG="overlay-hello.conf;overlay-gpio-test.conf" \
  -DPM_STATIC_YML_FILE:FILEPATH="${APP_DIR}/pm_static_rak4631_nrf52840.yml"

west flash --runner pyocd --build-dir "${BUILD_DIR}" -- \
  --target nrf52840 --dev-id "${PROBE_ID}"

RUN_ID="$(python3 "${ROOT_DIR}/tools/capture_rtt_run_id.py" --probe "${PROBE_ID}" --timeout 40 --logfile "${RTT_LOG}")"
echo "run_id=${RUN_ID}"

python3 "${ROOT_DIR}/tools/mqtt_wait_for_run_id.py" --run-id "${RUN_ID}" --timeout 90 --outfile "${PAYLOAD_JSON}"

DEVICE_ID="$(python3 - <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as f:
    data = json.load(f)
print(data.get("device_id", ""))
PY
"${PAYLOAD_JSON}")"

EVENT_ID="$(python3 - <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as f:
    data = json.load(f)
print(data.get("event_id", ""))
PY
"${PAYLOAD_JSON}")"

TIMESTAMP_MS="$(python3 - <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as f:
    data = json.load(f)
print(data.get("timestamp", ""))
PY
"${PAYLOAD_JSON}")"

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

python3 "${ROOT_DIR}/tools/e2e_verify_dynamodb.py" \
  --device-id "${DEVICE_ID}" \
  --run-id "${RUN_ID}" \
  --event-id "${EVENT_ID}" \
  --since-ms "$((TIMESTAMP_MS - 600000))" \
  --region "${REGION}" \
  --table "${PROJECT_PREFIX}-device_events_v2"
echo "PASS: ${SCRIPT_NAME}"
