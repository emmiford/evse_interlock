import os
import boto3
from collections import defaultdict

dynamodb = boto3.resource("dynamodb")
EVENTS_TABLE = os.environ["DEVICE_EVENTS_TABLE"]


def lambda_handler(event, context):
    """
    Expected event:
    {
      "device_id": "...",
      "start_ms": 1700000000000,
      "end_ms": 1700003600000,
      "bucket_ms": 3600000
    }
    """
    device_id = event["device_id"]
    start_ms = int(event["start_ms"])
    end_ms = int(event["end_ms"])
    bucket_ms = int(event.get("bucket_ms", 3600000))

    table = dynamodb.Table(EVENTS_TABLE)
    resp = table.query(
        KeyConditionExpression="device_id = :d AND timestamp BETWEEN :s AND :e",
        ExpressionAttributeValues={":d": device_id, ":s": start_ms, ":e": end_ms},
    )
    buckets = defaultdict(float)
    for item in resp.get("Items", []):
        ts = int(item.get("timestamp", 0))
        energy = float(item.get("energy_delivered_kwh", 0)) + float(
            item.get("energy_consumed_kwh", 0)
        )
        bucket = ts - (ts % bucket_ms)
        buckets[bucket] += energy
    return {"buckets": dict(buckets)}
