#!/usr/bin/env bash
set -euo pipefail
SCRIPT_NAME="$(basename "$0")"
trap 'echo "FAIL: ${SCRIPT_NAME}" >&2' ERR

REGION="${AWS_REGION:-us-east-1}"
PROJECT_PREFIX="${PROJECT_PREFIX:-sidewalk-v1}"
TABLE_NAME="${PROJECT_PREFIX}-device_events_idempotency"
DEVICE_ID="${DEVICE_ID:-test-device}"
EVENT_ID="${EVENT_ID:-$(python3 - <<'PY'
import uuid
print(uuid.uuid4())
PY
)}"

aws --region "${REGION}" dynamodb create-table \
  --table-name "${TABLE_NAME}" \
  --attribute-definitions \
    AttributeName=device_id,AttributeType=S \
    AttributeName=event_id,AttributeType=S \
  --key-schema \
    AttributeName=device_id,KeyType=HASH \
    AttributeName=event_id,KeyType=RANGE \
  --billing-mode PAY_PER_REQUEST \
  >/dev/null 2>&1 || true

aws --region "${REGION}" dynamodb wait table-exists --table-name "${TABLE_NAME}"

aws --region "${REGION}" dynamodb put-item \
  --table-name "${TABLE_NAME}" \
  --item "{\"device_id\":{\"S\":\"${DEVICE_ID}\"},\"event_id\":{\"S\":\"${EVENT_ID}\"}}" \
  --condition-expression "attribute_not_exists(event_id)" \
  >/dev/null

if aws --region "${REGION}" dynamodb put-item \
  --table-name "${TABLE_NAME}" \
  --item "{\"device_id\":{\"S\":\"${DEVICE_ID}\"},\"event_id\":{\"S\":\"${EVENT_ID}\"}}" \
  --condition-expression "attribute_not_exists(event_id)" \
  >/dev/null 2>&1; then
  echo "FAIL: idempotency condition did not reject duplicate" >&2
  exit 1
fi

echo "PASS: idempotency check"
echo "PASS: ${SCRIPT_NAME}"
