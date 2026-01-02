#!/usr/bin/env bash
set -euo pipefail

REGION="${AWS_REGION:-us-east-1}"
PROJECT_PREFIX="${PROJECT_PREFIX:-sidewalk-v1}"
OUTPUT_FILE="${OUTPUT_FILE:-aws_setup_output.txt}"

exec >"$OUTPUT_FILE" 2>&1

DEVICE_CONFIG_TABLE="${PROJECT_PREFIX}-device_config"

# Example seed for one device. Set these before running.
DEVICE_ID="${DEVICE_ID:-}"
DEVICE_TYPE="${DEVICE_TYPE:-evse}"   # evse | thermostat
SITE_ID="${SITE_ID:-}"
RATED_VOLTAGE_V="${RATED_VOLTAGE_V:-240}"
RATED_POWER_HEATING_KW="${RATED_POWER_HEATING_KW:-0}"
RATED_POWER_COOLING_KW="${RATED_POWER_COOLING_KW:-0}"

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

aws --region "$REGION" dynamodb put-item \
  --table-name "$DEVICE_CONFIG_TABLE" \
  --item "{
    \"device_id\": {\"S\": \"${DEVICE_ID}\"},
    \"device_type\": {\"S\": \"${DEVICE_TYPE}\"},
    \"site_id\": {\"S\": \"${SITE_ID}\"},
    \"rated_voltage_v\": {\"N\": \"${RATED_VOLTAGE_V}\"},
    \"rated_power_heating_kw\": {\"N\": \"${RATED_POWER_HEATING_KW}\"},
    \"rated_power_cooling_kw\": {\"N\": \"${RATED_POWER_COOLING_KW}\"},
    \"updated_at\": {\"N\": \"$(date +%s)\"}
  }"

echo "== device_config seeded =="
