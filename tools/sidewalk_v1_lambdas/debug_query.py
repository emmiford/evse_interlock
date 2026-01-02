import os
import boto3

dynamodb = boto3.resource("dynamodb")
EVENTS_TABLE = os.environ["DEVICE_EVENTS_TABLE"]


def lambda_handler(event, context):
    """
    Expected event:
    {
      "device_id": "...",
      "start_ms": 1700000000000,
      "end_ms": 1700003600000
    }
    """
    device_id = event["device_id"]
    start_ms = int(event["start_ms"])
    end_ms = int(event["end_ms"])

    table = dynamodb.Table(EVENTS_TABLE)
    resp = table.query(
        KeyConditionExpression="device_id = :d AND timestamp_ms BETWEEN :s AND :e",
        ExpressionAttributeValues={":d": device_id, ":s": start_ms, ":e": end_ms},
    )
    return {"items": resp.get("Items", [])}
