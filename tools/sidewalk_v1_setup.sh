#!/usr/bin/env bash
set -euo pipefail
SCRIPT_NAME="$(basename "$0")"
trap 'echo "FAIL: ${SCRIPT_NAME}" >&2' ERR

REGION="${AWS_REGION:-us-east-1}"
ACCOUNT_ID="$(aws sts get-caller-identity --query Account --output text)"
PROJECT_PREFIX="${PROJECT_PREFIX:-sidewalk-v1}"
OUTPUT_FILE="${OUTPUT_FILE:-aws_setup_output.txt}"

exec >"$OUTPUT_FILE" 2>&1

DEVICE_EVENTS_TABLE="${PROJECT_PREFIX}-device_events"
DEVICE_CONFIG_TABLE="${PROJECT_PREFIX}-device_config"
ARCHIVE_BUCKET="${PROJECT_PREFIX}-device-events-archive-${ACCOUNT_ID}-${REGION}"

echo "== Create DynamoDB tables =="
aws --region "$REGION" dynamodb create-table \
  --table-name "$DEVICE_EVENTS_TABLE" \
  --attribute-definitions \
    AttributeName=device_id,AttributeType=S \
    AttributeName=timestamp,AttributeType=N \
  --key-schema \
    AttributeName=device_id,KeyType=HASH \
    AttributeName=timestamp,KeyType=RANGE \
  --billing-mode PAY_PER_REQUEST \
  --stream-specification StreamEnabled=true,StreamViewType=NEW_IMAGE \
  || true

aws --region "$REGION" dynamodb update-time-to-live \
  --table-name "$DEVICE_EVENTS_TABLE" \
  --time-to-live-specification "Enabled=true,AttributeName=ttl" \
  || true

aws --region "$REGION" dynamodb create-table \
  --table-name "$DEVICE_CONFIG_TABLE" \
  --attribute-definitions AttributeName=device_id,AttributeType=S \
  --key-schema AttributeName=device_id,KeyType=HASH \
  --billing-mode PAY_PER_REQUEST \
  || true

echo "== Create S3 archive bucket =="
if [[ "$REGION" == "us-east-1" ]]; then
  aws --region "$REGION" s3api create-bucket \
    --bucket "$ARCHIVE_BUCKET" \
    || true
else
  aws --region "$REGION" s3api create-bucket \
    --bucket "$ARCHIVE_BUCKET" \
    --create-bucket-configuration LocationConstraint="$REGION" \
    || true
fi

aws --region "$REGION" s3api put-bucket-lifecycle-configuration \
  --bucket "$ARCHIVE_BUCKET" \
  --lifecycle-configuration '{
    "Rules": [{
      "ID": "archive-transitions",
      "Status": "Enabled",
      "Filter": {"Prefix": ""},
      "Transitions": [
        {"Days": 30, "StorageClass": "STANDARD_IA"},
        {"Days": 365, "StorageClass": "GLACIER"}
      ]
    }]
  }' || true

echo "== Create IAM role for IoT Rule =="
IOT_RULE_ROLE="${PROJECT_PREFIX}-iot-rule-role"
aws --region "$REGION" iam create-role \
  --role-name "$IOT_RULE_ROLE" \
  --assume-role-policy-document '{
    "Version":"2012-10-17",
    "Statement":[{"Effect":"Allow","Principal":{"Service":"iot.amazonaws.com"},"Action":"sts:AssumeRole"}]
  }' || true

aws --region "$REGION" iam put-role-policy \
  --role-name "$IOT_RULE_ROLE" \
  --policy-name "${PROJECT_PREFIX}-iot-rule-policy" \
  --policy-document "{
    \"Version\":\"2012-10-17\",
    \"Statement\":[
      {\"Effect\":\"Allow\",\"Action\":[\"dynamodb:PutItem\"],\"Resource\":\"arn:aws:dynamodb:${REGION}:${ACCOUNT_ID}:table/${DEVICE_EVENTS_TABLE}\"},
      {\"Effect\":\"Allow\",\"Action\":[\"logs:CreateLogGroup\",\"logs:CreateLogStream\",\"logs:PutLogEvents\"],\"Resource\":\"*\"}
    ]
  }" || true

echo "== Create IoT Rule (sidewalk/# -> DynamoDB) =="
# IoT Rule names must match ^[a-zA-Z0-9_]+$
RULE_PREFIX="${PROJECT_PREFIX//-/_}"
RULE_NAME="${RULE_PREFIX}_sidewalk_events_to_dynamodb"
aws --region "$REGION" iot create-topic-rule \
  --rule-name "$RULE_NAME" \
  --topic-rule-payload "{
    \"sql\": \"SELECT * FROM 'sidewalk/#'\",
    \"awsIotSqlVersion\": \"2016-03-23\",
    \"actions\": [{
      \"dynamoDBv2\": {
        \"roleArn\": \"arn:aws:iam::${ACCOUNT_ID}:role/${IOT_RULE_ROLE}\",
        \"putItem\": {\"tableName\": \"${DEVICE_EVENTS_TABLE}\"}
      }
    }],
    \"errorAction\": {
      \"cloudwatchLogs\": {
        \"logGroupName\": \"/aws/iot/${RULE_NAME}\",
        \"roleArn\": \"arn:aws:iam::${ACCOUNT_ID}:role/${IOT_RULE_ROLE}\"
      }
    }
  }" || true

echo "== Create IAM role for archive Lambda =="
ARCHIVE_ROLE="${PROJECT_PREFIX}-archive-lambda-role"
aws --region "$REGION" iam create-role \
  --role-name "$ARCHIVE_ROLE" \
  --assume-role-policy-document '{
    "Version":"2012-10-17",
    "Statement":[{"Effect":"Allow","Principal":{"Service":"lambda.amazonaws.com"},"Action":"sts:AssumeRole"}]
  }' || true

aws --region "$REGION" iam attach-role-policy \
  --role-name "$ARCHIVE_ROLE" \
  --policy-arn arn:aws:iam::aws:policy/service-role/AWSLambdaBasicExecutionRole \
  || true

aws --region "$REGION" iam put-role-policy \
  --role-name "$ARCHIVE_ROLE" \
  --policy-name "${PROJECT_PREFIX}-archive-ddb-s3" \
  --policy-document "{
    \"Version\":\"2012-10-17\",
    \"Statement\":[
      {\"Effect\":\"Allow\",\"Action\":[\"dynamodb:DescribeStream\",\"dynamodb:GetRecords\",\"dynamodb:GetShardIterator\",\"dynamodb:ListStreams\"],\"Resource\":\"*\"},
      {\"Effect\":\"Allow\",\"Action\":[\"s3:PutObject\"],\"Resource\":\"arn:aws:s3:::${ARCHIVE_BUCKET}/*\"}
    ]
  }" || true

echo "== Create archive Lambda and stream mapping =="
ARCHIVE_FN="${PROJECT_PREFIX}-archive-device-events"
LAMBDA_ZIP="/tmp/archive_device_events.zip"

zip -j "$LAMBDA_ZIP" tools/sidewalk_v1_lambdas/archive_device_events.py >/dev/null

aws --region "$REGION" lambda create-function \
  --function-name "$ARCHIVE_FN" \
  --runtime python3.11 \
  --role "arn:aws:iam::${ACCOUNT_ID}:role/${ARCHIVE_ROLE}" \
  --handler archive_device_events.lambda_handler \
  --zip-file "fileb://${LAMBDA_ZIP}" \
  --environment "Variables={ARCHIVE_BUCKET=${ARCHIVE_BUCKET}}" \
  || true

STREAM_ARN="$(aws --region "$REGION" dynamodb describe-table \
  --table-name "$DEVICE_EVENTS_TABLE" \
  --query 'Table.LatestStreamArn' --output text)"

aws --region "$REGION" lambda create-event-source-mapping \
  --function-name "$ARCHIVE_FN" \
  --event-source-arn "$STREAM_ARN" \
  --starting-position LATEST \
  || true

echo "== Create Athena database/table =="
ATHENA_DB="${PROJECT_PREFIX//-/_}"
ATHENA_TABLE="device_events_archive"
ATHENA_OUTPUT="s3://${ARCHIVE_BUCKET}/athena-results/"

aws --region "$REGION" athena start-query-execution \
  --query-string "CREATE SCHEMA IF NOT EXISTS ${ATHENA_DB}" \
  --result-configuration "OutputLocation=${ATHENA_OUTPUT}" \
  >/dev/null

aws --region "$REGION" athena start-query-execution \
  --query-string "CREATE EXTERNAL TABLE IF NOT EXISTS ${ATHENA_DB}.${ATHENA_TABLE} (
    device_id string,
    timestamp bigint,
    device_type string,
    event_type string,
    site_id string,
    location string,
    run_id string,
    data string
  )
  PARTITIONED BY (year string, month string, day string)
  ROW FORMAT SERDE 'org.openx.data.jsonserde.JsonSerDe'
  LOCATION 's3://${ARCHIVE_BUCKET}/'
  TBLPROPERTIES ('has_encrypted_data'='false')" \
  --result-configuration "OutputLocation=${ATHENA_OUTPUT}" \
  >/dev/null

echo "== Setup complete =="
echo "PASS: ${SCRIPT_NAME}"
