#!/usr/bin/env bash
set -euo pipefail

REGION="${AWS_REGION:-us-east-1}"
PROJECT_PREFIX="${PROJECT_PREFIX:-sidewalk-v1}"
OUTPUT_FILE="${OUTPUT_FILE:-aws_setup_output.txt}"
DEVICE_ID="${DEVICE_ID:-}"

exec >"$OUTPUT_FILE" 2>&1

if [[ -z "$DEVICE_ID" ]]; then
  echo "Set DEVICE_ID before running."
  exit 1
fi

echo "== Step 1: publish + verify DynamoDB v2 =="
bash tools/sidewalk_v1_publish_verify_endpoint.sh

echo "== Step 2: verify S3 archive write =="
bash tools/sidewalk_v1_verify_archive_s3.sh

echo "== Step 3: idempotency check =="
bash tools/e2e_idempotency_check.sh

echo "== E2E cloud verification complete =="
