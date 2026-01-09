# Architecture

This document consolidates the AWS data model and the EVSE safety state
machine.

## System Overview

Data flow:

```
RAK4631 Device -> Amazon Sidewalk -> AWS IoT Core -> DynamoDB (hot, 90 days)
                                                   -> S3 (cold archival)
                                                   -> Lambda (queries/aggregations)
```

Firmware capabilities:
- Sidewalk connectivity (LoRa link).
- State-change-only event triggering.
- Debounce + edge detection.
- JSON telemetry payloads.

Line current monitoring notes:
- With a plain ADC input there is no change interrupt, so sampling is still required.
- Telemetry only emits on change, but periodic sampling detects that change.
- If hardware provides a comparator/alert pin (or the clamp is routed through one),
  sampling can be gated by the comparator interrupt.
- Otherwise the best options are a lower poll interval or adaptive sampling
  (slow when stable, faster after a change).

## Payloads

Current (GPIO event):
```
{
  "source": "gpio",
  "pin": "hvac",
  "state": 0,
  "edge": "rising",
  "uptime_ms": 123456,
  "run_id": null
}
```

EVSE target payload (DynamoDB-aligned):
```
{
  "device_type": "evse",
  "device_id": "RAK4630_001",
  "timestamp": 1704067200000,
  "location": "garage_1",
  "event_type": "state_change",
  "pilot_state": "C",
  "pwm_duty_cycle": 50.0,
  "current_draw": 16.0,
  "proximity_detected": true,
  "session_id": "550e8400-e29b-41d4-a716-446655440000",
  "energy_delivered_kwh": 5.2
}
```

Time sync:
- Until a time sync message is received, `timestamp` uses uptime_ms.
- Downlink: `{"cmd":"time_sync","epoch_ms":1704067200000}`
- Device computes `epoch_at_boot_ms = epoch_ms - uptime_ms`.

## AWS Data Model

DynamoDB `device_events`:
- PK: `device_id` (String)
- SK: `timestamp` (Number, epoch ms)
- GSI: `LocationTimeIndex` (location, timestamp)
- TTL: epoch seconds (90 days)

DynamoDB `device_config`:
- PK: `device_id`
- Attributes: `device_type`, `rated_voltage_v`, `rated_power_*`, `location`

S3 archival:
- Bucket: `device-events-archive`
- Path: `s3://device-events-archive/{year}/{month}/{day}/{device_id}/`
- Format: Parquet, partitioned by date + device_id

## EVSE Safety State Machine

Rule: any ambiguity => EV OFF.

Inputs:
- HVAC input from `hvac` (after Zephyr polarity).
- Timestamp (uptime or epoch).
- Debounce config.
- Queue overflow flag.

Outputs:
- `ev_allowed` (false => EV OFF).
- Fault flags and `time_anomaly`.

Core behavior:
- Start in AC_UNKNOWN, `ev_allowed=false`.
- Stable OFF for >= debounce_ms and no faults => `ev_allowed=true`.
- Stable ON => `ev_allowed=false`.
- Backward time, invalid debounce, queue overflow, or unknown input => EV OFF.

## J1772 Pilot Mapping

- State A: +12V (no vehicle)
- State B: +9V (vehicle connected, no charge)
- State C: +6V (charging requested)
- State D: +3V (ventilation required)
- State E/F: error conditions

## Thermostat (optional)

Signals:
- `heating_call` (Y1/Y2)
- `cooling_call` (Y)

Energy calculation:
- Firmware: EVSE uses voltage/current integration per session.
- Lambda: HVAC energy derived from rated power and cycle duration.

## Work Plan (V1)

1. DynamoDB tables + TTL
2. S3 bucket + lifecycle policies
3. IoT Core rule to DynamoDB
4. Lambda archival + aggregations
5. Device config seeding
6. End-to-end validation
