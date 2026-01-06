#!/usr/bin/env bash
set -euo pipefail
SCRIPT_NAME="$(basename "$0")"
trap 'echo "FAIL: ${SCRIPT_NAME}" >&2' ERR

REGION="${AWS_REGION:-us-east-1}"
PROJECT_PREFIX="${PROJECT_PREFIX:-sidewalk-v1}"
OUTPUT_FILE="${OUTPUT_FILE:-aws_setup_output.txt}"

ACCOUNT_ID="$(aws sts get-caller-identity --query Account --output text)"

exec >"$OUTPUT_FILE" 2>&1

V2_TABLE="${PROJECT_PREFIX}-device_events_v2"
DEDUPE_TABLE="${PROJECT_PREFIX}-device_events_dedupe_v2"
ARCHIVE_BUCKET="${PROJECT_PREFIX}-device-events-archive-${ACCOUNT_ID}-${REGION}"
RULE_PREFIX="${PROJECT_PREFIX//-/_}"
NEW_RULE_NAME="${RULE_PREFIX}_sidewalk_events_to_dynamodb_v2"
IOT_RULE_ROLE="${PROJECT_PREFIX}-iot-rule-role"
ARCHIVE_FN="${PROJECT_PREFIX}-archive-device-events-v2"
ARCHIVE_ROLE="${PROJECT_PREFIX}-archive-lambda-role"
INGEST_FN="${PROJECT_PREFIX}-ingest-device-events-v2"
INGEST_ROLE="${PROJECT_PREFIX}-ingest-lambda-role"

echo "== Create v2 DynamoDB table =="
aws --region "$REGION" dynamodb create-table \
  --table-name "$V2_TABLE" \
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
  --table-name "$V2_TABLE" \
  --time-to-live-specification "Enabled=true,AttributeName=ttl" \
  || true

echo "== Create dedupe table for v2 =="
aws --region "$REGION" dynamodb create-table \
  --table-name "$DEDUPE_TABLE" \
  --attribute-definitions \
    AttributeName=device_id,AttributeType=S \
    AttributeName=event_id,AttributeType=S \
  --key-schema \
    AttributeName=device_id,KeyType=HASH \
    AttributeName=event_id,KeyType=RANGE \
  --billing-mode PAY_PER_REQUEST \
  || true

echo "== Create ingest Lambda for v2 writes =="
aws --region "$REGION" iam create-role \
  --role-name "$INGEST_ROLE" \
  --assume-role-policy-document '{
    "Version":"2012-10-17",
    "Statement":[{"Effect":"Allow","Principal":{"Service":"lambda.amazonaws.com"},"Action":"sts:AssumeRole"}]
  }' || true

aws --region "$REGION" iam attach-role-policy \
  --role-name "$INGEST_ROLE" \
  --policy-arn arn:aws:iam::aws:policy/service-role/AWSLambdaBasicExecutionRole \
  || true

aws --region "$REGION" iam put-role-policy \
  --role-name "$INGEST_ROLE" \
  --policy-name "${PROJECT_PREFIX}-ingest-ddb" \
  --policy-document "{
    \"Version\":\"2012-10-17\",
    \"Statement\":[
      {\"Effect\":\"Allow\",\"Action\":[\"dynamodb:PutItem\"],\"Resource\":[
        \"arn:aws:dynamodb:${REGION}:${ACCOUNT_ID}:table/${V2_TABLE}\",
        \"arn:aws:dynamodb:${REGION}:${ACCOUNT_ID}:table/${DEDUPE_TABLE}\"
      ]}
    ]
  }" || true

INGEST_ZIP="/tmp/ingest_device_events_v2.zip"
zip -j "$INGEST_ZIP" tools/aws/sidewalk_v1_lambdas/ingest_device_event.py >/dev/null

aws --region "$REGION" lambda create-function \
  --function-name "$INGEST_FN" \
  --runtime python3.11 \
  --role "arn:aws:iam::${ACCOUNT_ID}:role/${INGEST_ROLE}" \
  --handler ingest_device_event.lambda_handler \
  --zip-file "fileb://${INGEST_ZIP}" \
  --environment "Variables={DEVICE_EVENTS_TABLE=${V2_TABLE},DEDUPE_TABLE=${DEDUPE_TABLE}}" \
  || true

echo "== Create IoT Rule to write to v2 =="
aws --region "$REGION" iot create-topic-rule \
  --rule-name "$NEW_RULE_NAME" \
  --topic-rule-payload "{
    \"sql\": \"SELECT * FROM 'sidewalk/#'\",
    \"awsIotSqlVersion\": \"2016-03-23\",
    \"actions\": [{
      \"lambda\": {
        \"functionArn\": \"arn:aws:lambda:${REGION}:${ACCOUNT_ID}:function:${INGEST_FN}\"
      }
    }],
    \"errorAction\": {
      \"cloudwatchLogs\": {
        \"logGroupName\": \"/aws/iot/${NEW_RULE_NAME}\",
        \"roleArn\": \"arn:aws:iam::${ACCOUNT_ID}:role/${IOT_RULE_ROLE}\"
      }
    }
  }" || true

aws --region "$REGION" lambda add-permission \
  --function-name "$INGEST_FN" \
  --statement-id "${NEW_RULE_NAME}-invoke" \
  --action "lambda:InvokeFunction" \
  --principal iot.amazonaws.com \
  --source-arn "arn:aws:iot:${REGION}:${ACCOUNT_ID}:rule/${NEW_RULE_NAME}" \
  >/dev/null 2>&1 || true

echo "== Create archive Lambda for v2 stream =="
LAMBDA_ZIP="/tmp/archive_device_events_v2.zip"
zip -j "$LAMBDA_ZIP" tools/aws/sidewalk_v1_lambdas/archive_device_events.py >/dev/null

aws --region "$REGION" lambda create-function \
  --function-name "$ARCHIVE_FN" \
  --runtime python3.11 \
  --role "arn:aws:iam::${ACCOUNT_ID}:role/${ARCHIVE_ROLE}" \
  --handler archive_device_events.lambda_handler \
  --zip-file "fileb://${LAMBDA_ZIP}" \
  --environment "Variables={ARCHIVE_BUCKET=${ARCHIVE_BUCKET}}" \
  || true

STREAM_ARN="$(aws --region "$REGION" dynamodb describe-table \
  --table-name "$V2_TABLE" \
  --query 'Table.LatestStreamArn' --output text)"

aws --region "$REGION" lambda create-event-source-mapping \
  --function-name "$ARCHIVE_FN" \
  --event-source-arn "$STREAM_ARN" \
  --starting-position LATEST \
  || true

echo "== Create Athena schema/table for v2 =="
ATHENA_DB="${PROJECT_PREFIX//-/_}"
ATHENA_TABLE="device_events_archive_v2"
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

echo "== V2 setup complete =="
echo "PASS: ${SCRIPT_NAME}"
