#!/usr/bin/env bash
set -euo pipefail
SCRIPT_NAME="$(basename "$0")"
trap 'echo "FAIL: ${SCRIPT_NAME}" >&2' ERR

REGION="${AWS_REGION:-us-east-1}"
PROJECT_PREFIX="${PROJECT_PREFIX:-sidewalk-v1}"
OUTPUT_FILE="${OUTPUT_FILE:-aws_setup_output.txt}"

ACCOUNT_ID="$(aws sts get-caller-identity --query Account --output text)"

exec >"$OUTPUT_FILE" 2>&1

RULE_PREFIX="${PROJECT_PREFIX//-/_}"
RULE_NAME="${RULE_PREFIX}_sidewalk_events_to_dynamodb_v2"
LOG_GROUP="/aws/iot/${RULE_NAME}"

TOPIC_NAME="${PROJECT_PREFIX}-alarms"
TOPIC_ARN=""
if [[ -n "${ALARM_EMAIL:-}" ]]; then
  TOPIC_ARN="$(aws --region "$REGION" sns create-topic --name "$TOPIC_NAME" --query TopicArn --output text)"
  aws --region "$REGION" sns subscribe --topic-arn "$TOPIC_ARN" --protocol email --notification-endpoint "$ALARM_EMAIL" || true
fi

echo "== Create/ensure log group for IoT Rule errors =="
aws --region "$REGION" logs create-log-group --log-group-name "$LOG_GROUP" || true
aws --region "$REGION" logs put-retention-policy --log-group-name "$LOG_GROUP" --retention-in-days 14 || true

echo "== CloudWatch Alarms =="
declare -a ALARM_ACTIONS=()
if [[ -n "$TOPIC_ARN" ]]; then
  ALARM_ACTIONS=(--alarm-actions "$TOPIC_ARN")
fi

# IoT Rule error logs
aws --region "$REGION" cloudwatch put-metric-alarm \
  --alarm-name "${PROJECT_PREFIX}-iot-rule-errors" \
  --metric-name "IncomingMessages" \
  --namespace "AWS/IoT" \
  --statistic Sum \
  --period 300 \
  --threshold 1 \
  --comparison-operator GreaterThanOrEqualToThreshold \
  --dimensions Name=RuleName,Value="$RULE_NAME" \
  --evaluation-periods 1 \
  "${ALARM_ACTIONS[@]:-}" \
  || true

# Lambda errors
for FN in "${PROJECT_PREFIX}-archive-device-events-v2" \
          "${PROJECT_PREFIX}-hvac-energy-calc" \
          "${PROJECT_PREFIX}-debug-query" \
          "${PROJECT_PREFIX}-energy-aggregate"; do
  aws --region "$REGION" cloudwatch put-metric-alarm \
    --alarm-name "${FN}-errors" \
    --metric-name "Errors" \
    --namespace "AWS/Lambda" \
    --statistic Sum \
    --period 300 \
    --threshold 1 \
    --comparison-operator GreaterThanOrEqualToThreshold \
    --dimensions Name=FunctionName,Value="$FN" \
    --evaluation-periods 1 \
    "${ALARM_ACTIONS[@]:-}" \
    || true
done

# DynamoDB throttles (v2 table)
aws --region "$REGION" cloudwatch put-metric-alarm \
  --alarm-name "${PROJECT_PREFIX}-ddb-throttles" \
  --metric-name "ThrottledRequests" \
  --namespace "AWS/DynamoDB" \
  --statistic Sum \
  --period 300 \
  --threshold 1 \
  --comparison-operator GreaterThanOrEqualToThreshold \
  --dimensions Name=TableName,Value="${PROJECT_PREFIX}-device_events_v2" \
  --evaluation-periods 1 \
  "${ALARM_ACTIONS[@]:-}" \
  || true

echo "== CloudWatch Dashboard =="
cat > /tmp/${PROJECT_PREFIX}-dashboard.json <<'JSON'
{
  "widgets": [
    {
      "type": "metric",
      "x": 0,
      "y": 0,
      "width": 12,
      "height": 6,
      "properties": {
        "metrics": [
          [ "AWS/IoT", "IncomingMessages", "RuleName", "__RULE_NAME__" ]
        ],
        "period": 300,
        "stat": "Sum",
        "region": "__REGION__",
        "title": "IoT Rule Incoming Messages"
      }
    },
    {
      "type": "metric",
      "x": 12,
      "y": 0,
      "width": 12,
      "height": 6,
      "properties": {
        "metrics": [
          [ "AWS/DynamoDB", "ConsumedWriteCapacityUnits", "TableName", "__DDB_TABLE__" ]
        ],
        "period": 300,
        "stat": "Sum",
        "region": "__REGION__",
        "title": "DynamoDB Write Capacity"
      }
    },
    {
      "type": "metric",
      "x": 0,
      "y": 6,
      "width": 24,
      "height": 6,
      "properties": {
        "metrics": [
          [ "AWS/Lambda", "Errors", "FunctionName", "__LAMBDA_ARCHIVE__" ],
          [ ".", ".", "FunctionName", "__LAMBDA_HVAC__" ],
          [ ".", ".", "FunctionName", "__LAMBDA_DEBUG__" ],
          [ ".", ".", "FunctionName", "__LAMBDA_AGG__" ]
        ],
        "period": 300,
        "stat": "Sum",
        "region": "__REGION__",
        "title": "Lambda Errors"
      }
    }
  ]
}
JSON

sed -i.bak \
  -e "s|__RULE_NAME__|$RULE_NAME|g" \
  -e "s|__REGION__|$REGION|g" \
  -e "s|__DDB_TABLE__|${PROJECT_PREFIX}-device_events_v2|g" \
  -e "s|__LAMBDA_ARCHIVE__|${PROJECT_PREFIX}-archive-device-events-v2|g" \
  -e "s|__LAMBDA_HVAC__|${PROJECT_PREFIX}-hvac-energy-calc|g" \
  -e "s|__LAMBDA_DEBUG__|${PROJECT_PREFIX}-debug-query|g" \
  -e "s|__LAMBDA_AGG__|${PROJECT_PREFIX}-energy-aggregate|g" \
  /tmp/${PROJECT_PREFIX}-dashboard.json

aws --region "$REGION" cloudwatch put-dashboard \
  --dashboard-name "${PROJECT_PREFIX}-overview" \
  --dashboard-body "file:///tmp/${PROJECT_PREFIX}-dashboard.json" \
  || true

echo "== Optional: Budget (set BUDGET_EMAIL to enable) =="
if [[ -n "${BUDGET_EMAIL:-}" ]]; then
  aws budgets create-budget \
    --account-id "$ACCOUNT_ID" \
    --budget "{
      \"BudgetName\": \"${PROJECT_PREFIX}-monthly\",
      \"BudgetLimit\": {\"Amount\": \"15\", \"Unit\": \"USD\"},
      \"BudgetType\": \"COST\",
      \"TimeUnit\": \"MONTHLY\"
    }" \
    --notifications-with-subscribers "[
      {
        \"Notification\": {
          \"NotificationType\": \"ACTUAL\",
          \"ComparisonOperator\": \"GREATER_THAN\",
          \"Threshold\": 80,
          \"ThresholdType\": \"PERCENTAGE\"
        },
        \"Subscribers\": [
          {\"SubscriptionType\": \"EMAIL\", \"Address\": \"${BUDGET_EMAIL}\"}
        ]
      }
    ]" || true
fi

echo "== Monitoring setup complete =="
echo "PASS: ${SCRIPT_NAME}"
