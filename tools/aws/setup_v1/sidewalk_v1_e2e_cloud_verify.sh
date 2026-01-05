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

echo "== Step 1: publish + verify DynamoDB v2 =="
bash tools/aws/setup_v1/sidewalk_v1_publish_verify_endpoint.sh

echo "== Step 2: verify S3 archive write =="
bash tools/aws/setup_v1/sidewalk_v1_verify_archive_s3.sh

echo "== Step 3: idempotency check =="
bash tests/e2e_idempotency_check.sh

echo "== E2E cloud verification complete =="
echo "PASS: ${SCRIPT_NAME}"
