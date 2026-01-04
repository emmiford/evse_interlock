#!/usr/bin/env python3
import json
import os
from decimal import Decimal

import boto3
from boto3.dynamodb.types import TypeSerializer
from botocore.exceptions import ClientError

DEVICE_EVENTS_TABLE = os.environ.get("DEVICE_EVENTS_TABLE")
DEDUPE_TABLE = os.environ.get("DEDUPE_TABLE")
TTL_DAYS = 90


def _normalize(item):
    return json.loads(json.dumps(item), parse_float=Decimal)


def lambda_handler(event, context):
    if isinstance(event, str):
        event = json.loads(event)

    if isinstance(event, dict) and "message" in event and isinstance(event["message"], str):
        event = json.loads(event["message"])

    if not isinstance(event, dict):
        raise ValueError("invalid event payload")

    device_id = event.get("device_id")
    event_id = event.get("event_id")
    timestamp = event.get("timestamp")
    if not device_id or not event_id or timestamp is None:
        raise ValueError("missing required fields: device_id, event_id, timestamp")

    ts_ms = int(timestamp)
    ttl = ts_ms // 1000 + TTL_DAYS * 24 * 60 * 60

    if not DEVICE_EVENTS_TABLE or not DEDUPE_TABLE:
        raise ValueError("missing DEVICE_EVENTS_TABLE or DEDUPE_TABLE")

    ddb = boto3.client("dynamodb")
    try:
        ddb.put_item(
            TableName=DEDUPE_TABLE,
            Item={
                "device_id": {"S": device_id},
                "event_id": {"S": event_id},
                "timestamp": {"N": str(ts_ms)},
            },
            ConditionExpression="attribute_not_exists(event_id)",
        )
    except ClientError as exc:
        if exc.response["Error"]["Code"] == "ConditionalCheckFailedException":
            return {"status": "duplicate"}
        raise

    item = _normalize(event)
    item["ttl"] = ttl

    serializer = TypeSerializer()
    ddb.put_item(
        TableName=DEVICE_EVENTS_TABLE,
        Item={k: serializer.serialize(v) for k, v in item.items()},
    )

    return {"status": "ok"}
