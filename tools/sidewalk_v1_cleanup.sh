#!/usr/bin/env bash
set -euo pipefail
SCRIPT_NAME="$(basename "$0")"
trap 'echo "FAIL: ${SCRIPT_NAME}" >&2' ERR

REGION="${AWS_REGION:-us-east-1}"
PROJECT_PREFIX="${PROJECT_PREFIX:-sidewalk-v1}"
OUTPUT_FILE="${OUTPUT_FILE:-aws_setup_output.txt}"

exec >"$OUTPUT_FILE" 2>&1

RULE_PREFIX="${PROJECT_PREFIX//-/_}"
OLD_RULE_NAME="${RULE_PREFIX}_sidewalk_events_to_dynamodb"
OLD_TABLE="${PROJECT_PREFIX}-device_events"
OLD_ARCHIVE_FN="${PROJECT_PREFIX}-archive-device-events"

echo "== Disable + delete old IoT Rule =="
aws --region "$REGION" iot disable-topic-rule --rule-name "$OLD_RULE_NAME" || true
aws --region "$REGION" iot delete-topic-rule --rule-name "$OLD_RULE_NAME" || true

echo "== Delete old archive Lambda event source mappings =="
MAPPING_IDS=$(aws --region "$REGION" lambda list-event-source-mappings \
  --function-name "$OLD_ARCHIVE_FN" \
  --query 'EventSourceMappings[].UUID' --output text || true)

for ID in $MAPPING_IDS; do
  aws --region "$REGION" lambda delete-event-source-mapping --uuid "$ID" || true
done

echo "== Delete old archive Lambda =="
aws --region "$REGION" lambda delete-function --function-name "$OLD_ARCHIVE_FN" || true

echo "== Delete old DynamoDB table (DATA LOSS) =="
aws --region "$REGION" dynamodb delete-table --table-name "$OLD_TABLE" || true

echo "== Cleanup complete =="
echo "PASS: ${SCRIPT_NAME}"
