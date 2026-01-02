#!/usr/bin/env bash
set -euo pipefail

REGION="${AWS_REGION:-us-east-1}"
PROJECT_PREFIX="${PROJECT_PREFIX:-sidewalk-v1}"
OUTPUT_FILE="${OUTPUT_FILE:-aws_setup_output.txt}"

ACCOUNT_ID="$(aws sts get-caller-identity --query Account --output text)"
IOT_RULE_ROLE="${PROJECT_PREFIX}-iot-rule-role"
POLICY_NAME="${PROJECT_PREFIX}-iot-rule-policy"
V2_TABLE="${PROJECT_PREFIX}-device_events_v2"

exec >"$OUTPUT_FILE" 2>&1

echo "== Update IoT Rule role policy for v2 table =="
aws --region "$REGION" iam put-role-policy \
  --role-name "$IOT_RULE_ROLE" \
  --policy-name "$POLICY_NAME" \
  --policy-document "{
    \"Version\":\"2012-10-17\",
    \"Statement\":[
      {\"Effect\":\"Allow\",\"Action\":[\"dynamodb:PutItem\"],\"Resource\":\"arn:aws:dynamodb:${REGION}:${ACCOUNT_ID}:table/${V2_TABLE}\"},
      {\"Effect\":\"Allow\",\"Action\":[\"logs:CreateLogGroup\",\"logs:CreateLogStream\",\"logs:PutLogEvents\"],\"Resource\":\"*\"}
    ]
  }"

echo "== Policy update complete =="
