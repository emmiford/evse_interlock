#!/usr/bin/env bash
set -euo pipefail

REGION="${AWS_REGION:-us-east-1}"
PROJECT_PREFIX="${PROJECT_PREFIX:-sidewalk-v1}"
OUTPUT_FILE="${OUTPUT_FILE:-aws_setup_output.txt}"

exec >"$OUTPUT_FILE" 2>&1

DEVICE_ID="${DEVICE_ID:-}"
SITE_ID="${SITE_ID:-}"

if [[ -z "$DEVICE_ID" && -f "aws_wireless_device.json" ]]; then
  DEVICE_ID="$(python3 - <<'PY'
import json
with open("aws_wireless_device.json", "r", encoding="utf-8") as f:
    data = json.load(f)
print(data.get("Sidewalk", {}).get("SidewalkManufacturingSn", ""))
PY
)"
fi

if [[ -z "$SITE_ID" && -f "tools/aws_cleanup_and_tag.sh" ]]; then
  SITE_ID="$(python3 - <<'PY'
import re
with open("tools/aws_cleanup_and_tag.sh", "r", encoding="utf-8") as f:
    content = f.read()
m = re.search(r'^SITE_ID_SHORT="([^"]+)"', content, re.MULTILINE)
print(m.group(1) if m else "")
PY
)"
fi

if [[ -z "$DEVICE_ID" || -z "$SITE_ID" ]]; then
  echo "Set DEVICE_ID and SITE_ID before running."
  exit 1
fi

echo "== Publish test event to IoT Core =="
TS_MS="$(python3 - <<'PY'
import time
print(int(time.time() * 1000))
PY
)"
aws --region "$REGION" iot-data publish \
  --cli-binary-format raw-in-base64-out \
  --topic "sidewalk/test/${DEVICE_ID}/events" \
  --payload "{
    \"schema_version\":\"1.0\",
    \"device_id\":\"${DEVICE_ID}\",
    \"device_type\":\"evse\",
    \"timestamp_ms\":${TS_MS},
    \"event_type\":\"state_change\",
    \"site_id\":\"${SITE_ID}\",
    \"location\":null,
    \"run_id\":null,
    \"data\":{\"evse\":{\"pilot_state\":\"B\",\"pwm_duty_cycle\":50.0,\"current_draw_a\":0,\"proximity_detected\":true,\"session_id\":\"test\",\"energy_delivered_kwh\":0}}
  }"

echo "== Query DynamoDB for device_id =="
aws --region "$REGION" dynamodb query \
  --table-name "${PROJECT_PREFIX}-device_events" \
  --key-condition-expression "device_id = :d" \
  --expression-attribute-values "{\":d\":{\"S\":\"${DEVICE_ID}\"}}"

echo "== E2E test complete =="
