import os
import boto3

dynamodb = boto3.resource("dynamodb")
CONFIG_TABLE = os.environ["DEVICE_CONFIG_TABLE"]
EVENTS_TABLE = os.environ["DEVICE_EVENTS_TABLE"]


def lambda_handler(event, context):
    """
    Expected event:
    {
      "device_id": "...",
      "cycle_id": "...",
      "mode": "heating|cooling",
      "duration_hours": 0.25,
      "timestamp_ms": 1700000000000
    }
    """
    device_id = event["device_id"]
    mode = event["mode"]
    duration = float(event["duration_hours"])
    ts = int(event["timestamp_ms"])

    cfg = dynamodb.Table(CONFIG_TABLE).get_item(Key={"device_id": device_id}).get("Item", {})
    if mode == "heating":
        rated_kw = float(cfg.get("rated_power_heating_kw", 0))
    else:
        rated_kw = float(cfg.get("rated_power_cooling_kw", 0))
    energy_kwh = rated_kw * duration

    dynamodb.Table(EVENTS_TABLE).put_item(
        Item={
            "device_id": device_id,
            "timestamp_ms": ts,
            "device_type": "thermostat",
            "event_type": "cycle_end",
            "energy_consumed_kwh": energy_kwh,
            "cycle_id": event.get("cycle_id", "unknown"),
        }
    )
    return {"energy_kwh": energy_kwh}
