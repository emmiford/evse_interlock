#!/usr/bin/env python3
import json
import os
import time
import uuid
from decimal import Decimal

import boto3
from boto3.dynamodb.types import TypeSerializer
from botocore.exceptions import ClientError

DEVICE_EVENTS_TABLE = os.environ.get("DEVICE_EVENTS_TABLE")
DEDUPE_TABLE = os.environ.get("DEDUPE_TABLE")
TTL_DAYS = 90


def _normalize(item):
    return json.loads(json.dumps(item), parse_float=Decimal)

def _parse_event(event):
    if isinstance(event, str):
        try:
            return json.loads(event)
        except json.JSONDecodeError:
            return {"raw": event}

    if isinstance(event, dict) and "message" in event and isinstance(event["message"], str):
        try:
            return json.loads(event["message"])
        except json.JSONDecodeError:
            return {"raw": event["message"]}

    if isinstance(event, dict):
        return event

    return {"raw": event}

def _coerce_int(value, default):
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def lambda_handler(event, context):
    event = _parse_event(event)

    device_id = (
        event.get("device_id")
        or event.get("deviceId")
        or event.get("wireless_device_id")
        or event.get("WirelessDeviceId")
    )
    event_id = event.get("event_id") or event.get("eventId")
    timestamp = event.get("timestamp") or event.get("ts") or event.get("time")

    now_ms = int(time.time() * 1000)
    ts_ms = _coerce_int(timestamp, now_ms)
    if not device_id:
        device_id = "unknown"
    if not event_id:
        event_id = f"generated-{uuid.uuid4().hex}"

    event["device_id"] = str(device_id)
    event["event_id"] = str(event_id)
    event["timestamp"] = ts_ms
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
