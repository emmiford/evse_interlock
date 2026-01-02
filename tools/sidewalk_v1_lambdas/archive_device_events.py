import json
import os
import datetime
import boto3

s3 = boto3.client("s3")
ARCHIVE_BUCKET = os.environ["ARCHIVE_BUCKET"]


def lambda_handler(event, context):
    for rec in event.get("Records", []):
        if rec.get("eventName") not in ("INSERT", "MODIFY"):
            continue
        img = rec.get("dynamodb", {}).get("NewImage", {})
        ts = int(img.get("timestamp_ms", {}).get("N", "0"))
        day = datetime.datetime.utcfromtimestamp(ts / 1000).strftime("%Y/%m/%d")
        device_id = img.get("device_id", {}).get("S", "unknown")
        key = f"{day}/{device_id}/{ts}.json"
        s3.put_object(Bucket=ARCHIVE_BUCKET, Key=key, Body=json.dumps(img))
    return {"ok": True}
