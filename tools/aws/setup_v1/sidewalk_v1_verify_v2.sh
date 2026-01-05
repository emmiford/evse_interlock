#!/usr/bin/env bash
set -euo pipefail
SCRIPT_NAME="$(basename "$0")"
trap 'echo "FAIL: ${SCRIPT_NAME}" >&2' ERR

REGION="${AWS_REGION:-us-east-1}"
PROJECT_PREFIX="${PROJECT_PREFIX:-sidewalk-v1}"
OUTPUT_FILE="${OUTPUT_FILE:-aws_setup_output.txt}"
DEVICE_ID="${DEVICE_ID:-}"

exec >"$OUTPUT_FILE" 2>&1

if [[ -z "$DEVICE_ID" ]]; then
  echo "Set DEVICE_ID before running."
  exit 1
fi

echo "== Publish test event =="
TS_MS="$(python3 - <<'PY'
import time
print(int(time.time() * 1000))
PY
)"
EVENT_ID="$(python3 - <<'PY'
import uuid
print(uuid.uuid4())
PY
)"

aws --region "$REGION" iot-data publish \
  --cli-binary-format raw-in-base64-out \
  --topic "sidewalk/test/${DEVICE_ID}/events" \
  --payload "{
    \"schema_version\":\"1.0\",
    \"device_id\":\"${DEVICE_ID}\",
    \"device_type\":\"evse\",
    \"timestamp\":${TS_MS},
    \"event_id\":\"${EVENT_ID}\",
    \"time_anomaly\":false,
    \"event_type\":\"state_change\",
    \"location\":null,
    \"run_id\":null,
    \"data\":{\"evse\":{\"pilot_state\":\"B\",\"pwm_duty_cycle\":50.0,\"current_draw\":0,\"proximity_detected\":true,\"session_id\":\"test\",\"energy_delivered_kwh\":0}}
  }"

echo "== Query v2 table =="
aws --region "$REGION" dynamodb query \
  --table-name "${PROJECT_PREFIX}-device_events_v2" \
  --key-condition-expression "device_id = :d" \
  --expression-attribute-values "{\":d\":{\"S\":\"${DEVICE_ID}\"}}"

echo "== Verify complete =="
echo "PASS: ${SCRIPT_NAME}"
