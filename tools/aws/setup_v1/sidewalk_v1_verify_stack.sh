#!/usr/bin/env bash
set -euo pipefail
SCRIPT_NAME="$(basename "$0")"
trap 'echo "FAIL: ${SCRIPT_NAME}" >&2' ERR

REGION="${AWS_REGION:-us-east-1}"
PROJECT_PREFIX="${PROJECT_PREFIX:-sidewalk-v1}"
OUTPUT_FILE="${OUTPUT_FILE:-aws_setup_output.txt}"
DEVICE_ID="${DEVICE_ID:-}"

exec >"$OUTPUT_FILE" 2>&1

RULE_PREFIX="${PROJECT_PREFIX//-/_}"
RULE_NAME="${RULE_PREFIX}_sidewalk_events_to_dynamodb_v2"
TABLE_NAME="${PROJECT_PREFIX}-device_events_v2"

echo "== Check IoT Rule status =="
aws --region "$REGION" iot get-topic-rule --rule-name "$RULE_NAME"

echo "== Check IoT data endpoint =="
aws --region "$REGION" iot describe-endpoint --endpoint-type iot:Data-ATS

echo "== Check DynamoDB table status =="
aws --region "$REGION" dynamodb describe-table --table-name "$TABLE_NAME" \
  --query 'Table.TableStatus' --output text

echo "== Query recent items for DEVICE_ID (if set) =="
if [[ -n "$DEVICE_ID" ]]; then
  aws --region "$REGION" dynamodb query \
    --table-name "$TABLE_NAME" \
    --key-condition-expression "device_id = :d" \
    --expression-attribute-values "{\":d\":{\"S\":\"${DEVICE_ID}\"}}" \
    --limit 5
else
  echo "DEVICE_ID not set; skipping query."
fi

echo "== Check CloudWatch log group for rule errors =="
LOG_GROUP="/aws/iot/${RULE_NAME}"
aws --region "$REGION" logs describe-log-groups \
  --log-group-name-prefix "$LOG_GROUP"

echo "== Verify complete =="
echo "PASS: ${SCRIPT_NAME}"
