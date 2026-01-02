#!/usr/bin/env bash
set -euo pipefail

REGION="${AWS_REGION:-us-east-1}"
ACCOUNT_ID="$(aws sts get-caller-identity --query Account --output text)"
PROJECT_PREFIX="${PROJECT_PREFIX:-sidewalk-v1}"
OUTPUT_FILE="${OUTPUT_FILE:-aws_setup_output.txt}"

exec >"$OUTPUT_FILE" 2>&1

ROLE_NAME="${PROJECT_PREFIX}-query-lambda-role"
DEVICE_EVENTS_TABLE="${PROJECT_PREFIX}-device_events_v2"
DEVICE_CONFIG_TABLE="${PROJECT_PREFIX}-device_config"

aws --region "$REGION" iam create-role \
  --role-name "$ROLE_NAME" \
  --assume-role-policy-document '{
    "Version":"2012-10-17",
    "Statement":[{"Effect":"Allow","Principal":{"Service":"lambda.amazonaws.com"},"Action":"sts:AssumeRole"}]
  }' || true

aws --region "$REGION" iam attach-role-policy \
  --role-name "$ROLE_NAME" \
  --policy-arn arn:aws:iam::aws:policy/service-role/AWSLambdaBasicExecutionRole \
  || true

aws --region "$REGION" iam put-role-policy \
  --role-name "$ROLE_NAME" \
  --policy-name "${PROJECT_PREFIX}-query-ddb" \
  --policy-document "{
    \"Version\":\"2012-10-17\",
    \"Statement\":[
      {\"Effect\":\"Allow\",\"Action\":[\"dynamodb:GetItem\",\"dynamodb:Query\",\"dynamodb:PutItem\"],\"Resource\":[
        \"arn:aws:dynamodb:${REGION}:${ACCOUNT_ID}:table/${DEVICE_EVENTS_TABLE}\",
        \"arn:aws:dynamodb:${REGION}:${ACCOUNT_ID}:table/${DEVICE_CONFIG_TABLE}\"
      ]}
    ]
  }" || true

deploy_fn() {
  local name="$1"
  local file="$2"
  local handler="$3"

  local zip="/tmp/${name}.zip"
  zip -j "$zip" "$file" >/dev/null

  aws --region "$REGION" lambda create-function \
    --function-name "$name" \
    --runtime python3.11 \
    --role "arn:aws:iam::${ACCOUNT_ID}:role/${ROLE_NAME}" \
    --handler "$handler" \
    --zip-file "fileb://${zip}" \
    --environment "Variables={DEVICE_EVENTS_TABLE=${DEVICE_EVENTS_TABLE},DEVICE_CONFIG_TABLE=${DEVICE_CONFIG_TABLE}}" \
    || aws --region "$REGION" lambda update-function-code \
      --function-name "$name" \
      --zip-file "fileb://${zip}"
}

deploy_fn "${PROJECT_PREFIX}-hvac-energy-calc" tools/sidewalk_v1_lambdas/hvac_energy_calc.py hvac_energy_calc.lambda_handler
deploy_fn "${PROJECT_PREFIX}-debug-query" tools/sidewalk_v1_lambdas/debug_query.py debug_query.lambda_handler
deploy_fn "${PROJECT_PREFIX}-energy-aggregate" tools/sidewalk_v1_lambdas/energy_aggregate.py energy_aggregate.lambda_handler

echo "== Lambda deploy complete =="
