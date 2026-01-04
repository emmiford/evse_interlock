#!/usr/bin/env bash
set -euo pipefail
SCRIPT_NAME="$(basename "$0")"
trap 'echo "FAIL: ${SCRIPT_NAME}" >&2' ERR

REGION="${AWS_REGION:-us-east-1}"
PROJECT_PREFIX="${PROJECT_PREFIX:-sidewalk-v1}"
OUTPUT_FILE="${OUTPUT_FILE:-aws_setup_output.txt}"

exec >"$OUTPUT_FILE" 2>&1

DEVICE_EVENTS_TABLE="${PROJECT_PREFIX}-device_events_v2"
DEVICE_CONFIG_TABLE="${PROJECT_PREFIX}-device_config"

update_env() {
  local fn="$1"
  aws --region "$REGION" lambda update-function-configuration \
    --function-name "$fn" \
    --environment "Variables={DEVICE_EVENTS_TABLE=${DEVICE_EVENTS_TABLE},DEVICE_CONFIG_TABLE=${DEVICE_CONFIG_TABLE}}"
}

echo "== Update lambda env to v2 table =="
update_env "${PROJECT_PREFIX}-hvac-energy-calc"
update_env "${PROJECT_PREFIX}-debug-query"
update_env "${PROJECT_PREFIX}-energy-aggregate"

echo "== Lambda env update complete =="
echo "PASS: ${SCRIPT_NAME}"
