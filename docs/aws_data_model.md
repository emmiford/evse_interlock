# AWS Data Model: J1772 EVSE + Thermostat Control Wire Logging

## Overview

Cost-optimized, event-driven pipeline for Sidewalk devices (RAK4630) sending state-change telemetry into AWS IoT Core, DynamoDB (hot 90-day retention), and S3 (cold archival). The model preserves a future migration path to time-series databases (e.g., Timestream/InfluxDB) without rewriting firmware payloads.

Data Flow:

RAK4630 Device -> Amazon Sidewalk Network -> AWS IoT Core -> DynamoDB (hot data, 90 days) -> S3 (cold archival)
                                                           |
                                                           +-> Lambda (queries/aggregations)

## Firmware Integration (evse_interlock repo)

Current capabilities:
- Sidewalk -> AWS IoT Core connectivity
- State-change-only event triggering
- Debounce logic (default 50ms)
- Edge detection (rising/falling)
- JSON payload formatting

Required enhancements (EVSE-specific):
1) J1772 pilot voltage measurement and state mapping (A/B/C/D/E/F)
2) PWM duty cycle measurement (10-96%)
3) Current sensing integration
4) Proximity detection (GPIO)
5) Session state machine (UUID, start/end)
6) Energy calculation (kWh per session)
7) Payload format alignment with DynamoDB schema

EVSE firmware notes:
- PWM duty cycle uses GPIO edge timestamps (timer-based) for a first pass.
- ADC channels and GPIO pins are configurable via Kconfig.
- `timestamp` switches from uptime to epoch after time sync downlink.

## Payload Format

Current (generic GPIO):
{
  "source": "gpio",
  "pin": "extinput0",
  "state": 0,
  "edge": "rising",
  "uptime_ms": 123456,
  "run_id": "test123"
}

Required EVSE payload (aligned to DynamoDB):
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

Payload field mapping:
- device_type: hardcoded "evse" in firmware
- device_id: Sidewalk device identifier
- timestamp: Unix epoch ms (from uptime + base time)
- location: device_config source of truth
- event_type: state_change | session_start | session_end
- pilot_state: derived from pilot voltage
- pwm_duty_cycle: measured from PWM
- current_draw: measured current
- proximity_detected: GPIO
- session_id: UUID at State B
- energy_delivered_kwh: cumulative per session

Optional debug:
- run_id (E2E debug only)

Time sync:
- Until a time sync message is received, `timestamp` uses uptime_ms for ordering.
- Downlink format: {"cmd":"time_sync","epoch_ms":1704067200000}
- Device computes epoch_at_boot_ms = epoch_ms - uptime_ms.

IoT Rule adds:
- ttl: epoch seconds (now + 90 days)
- validation for pilot_state and pwm_duty_cycle range

## J1772 Pilot State Mapping

- State A: +12V (no vehicle)
- State B: +9V (vehicle connected, no charge)
- State C: +6V (charging requested)
- State D: +3V (ventilation required)
- State E: 0V (error)
- State F: -12V (error/unavailable)

## Thermostat Control Wire (State-change Logging)

Signals:
- heating_call (Y1/Y2)
- cooling_call (Y)

Logging: state transitions only.

## Energy Calculation Responsibility

Firmware (EVSE):
- energy_delivered_kwh = integral(voltage * current * time)
- reset at session start, cumulative reported each state_change

Lambda (HVAC):
- energy_consumed_kwh = rated_power_kw * cycle_duration_hours
- computed at cycle_end by querying cycle_start
- rated power from device_config

## DynamoDB Schema

Table: device_events
- PK: device_id (String)
- SK: timestamp (Number, epoch ms)
- GSI LocationTimeIndex: (location, timestamp)

Attributes:
- device_type (String): evse | thermostat
- location (String)
- event_type (String): state_change | session_start | session_end | cycle_start | cycle_end
- ttl (Number): epoch seconds for auto-delete

EVSE attributes:
- pilot_state (String)
- pwm_duty_cycle (Number)
- current_draw (Number)
- proximity_detected (Boolean)
- session_id (String)
- energy_delivered_kwh (Number)

Thermostat attributes:
- heating_call (Boolean)
- cooling_call (Boolean)
- cycle_id (String)
- energy_consumed_kwh (Number)

Table: device_config
- PK: device_id (String)
- device_type (String)
- rated_voltage_v (Number)
- rated_power_heating_kw (Number)
- rated_power_cooling_kw (Number)
- location (String)
- created_at (Number)
- updated_at (Number)

## S3 Archival

Bucket: device-events-archive
Path: s3://device-events-archive/{year}/{month}/{day}/{device_id}/
Format: Parquet
Partitioning: date + device_id

## Retention

- DynamoDB: 90 days (TTL)
- S3: 90+ days; lifecycle to IA/Glacier

## Query Patterns

- Recent: DynamoDB query by device_id and timestamp range
- Historical: Athena over S3
- Aggregations: Lambda (daily/hourly/location)

## Cost Target

- 10 devices: ~$8-13/month (DDB + S3 + Lambda + IoT Core)

## Hardware Blocker (EVSE)

Current RAK4630 base board lacks sufficient I/O for full J1772 monitoring. Required:
- Base board with >=3 analog inputs and >=2 digital inputs
- Pilot voltage measurement circuit
- Current sensor
- Proximity detection circuit

## Firmware Configuration (EVSE)

Kconfig defaults are set for RAK19001:
- `CONFIG_SID_END_DEVICE_EVSE_PWM_GPIO_PORT=0`
- `CONFIG_SID_END_DEVICE_EVSE_PWM_GPIO_PIN=1`
- `CONFIG_SID_END_DEVICE_EVSE_PROX_GPIO_PORT=0`
- `CONFIG_SID_END_DEVICE_EVSE_PROX_GPIO_PIN=4`
- `CONFIG_SID_END_DEVICE_EVSE_PILOT_ADC_CHANNEL=2`
- `CONFIG_SID_END_DEVICE_EVSE_CURRENT_ADC_CHANNEL=3`
- `CONFIG_SID_END_DEVICE_EVSE_SAMPLE_INTERVAL_MS=2000`

Scale and bias:
- `CONFIG_SID_END_DEVICE_EVSE_PILOT_SCALE_NUM/DEN` and `..._BIAS_MV`
- `CONFIG_SID_END_DEVICE_EVSE_CURRENT_SCALE_NUM/DEN`

## Work Plan (V1 + Firmware)

1. Set up DynamoDB tables and configure TTL for device events
2. Create S3 bucket with lifecycle policies for event archival
3. Configure AWS IoT Core for Sidewalk device integration
4. Implement IoT Rules Engine to route messages to DynamoDB
5. Build Lambda function for S3 archival triggered by DynamoDB TTL
6. Implement Lambda function for HVAC energy calculation
7. Build Lambda functions for historical debugging queries
8. Build Lambda functions for energy aggregation (daily/hourly summaries)
9. Set up Athena table for querying S3 archived data
10. Populate device_config table with initial device configurations
11. Set up CloudWatch monitoring and cost tracking
12. End-to-end integration testing and validation
13. Design and implement J1772 pilot signal voltage measurement and state mapping
14. Implement PWM duty cycle measurement for J1772 pilot signal
15. Integrate current sensor for actual amperage measurement
16. Implement proximity detection for plug connection status
17. Implement session state machine with UUID generation and energy tracking
18. Update payload format to match DynamoDB schema and integrate with Sidewalk transmission

## Decisions

- Thermostat fan call excluded.
