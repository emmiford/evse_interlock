# Sidewalk GPIO Tests

## What input triggers E2E?
- Trigger: HVAC input (no readable on-board button on RAK4631).
- Alias: `hvac` (defined in `app/evse_interlock_v1/conf/rak4631.overlay`).
- Active level: active low (pull-up enabled in overlay).
- Events per press: 2 (rising + falling) when using a real input.
- E2E default: real HVAC input (simulator disabled).
- Logging: RTT is enabled in `app/evse_interlock_v1/conf/prj.conf`.

Note: RAK4631 DTS has no `gpio-keys`/`button0` aliases; reset is wired to nRESET and is not a readable GPIO.

## Test Plan (Pragmatic)

| Test Type | Runs On | Purpose |
|---------|--------|--------|
| Unit | macOS/Linux | Logic correctness |
| Zephyr Integration | Linux only | OS + drivers |
| E2E | Hardware | System validation |

## Safety Invariants (V1)

Hard requirements:
- HVAC asserted => EV OFF (no exceptions).
- Any ambiguity => EV OFF (unknown input, invalid transition, timestamp anomaly,
  queue overflow, missing init).
- Boot/reset/brownout => EV OFF by default.

Definitions:
- HVAC asserted means `hvac` reads logical high after Zephyr polarity.
- EV OFF means `ev_allowed == false` and the enable GPIO is deasserted.

Ambiguity cases (fail-safe):
- Input unknown/uninitialized
- Debounce invalid or out of range
- Backward time detected
- Queue overflow/dropped events
- Any internal fault flag set

## Test Layers (Safety Gate)

Layer 0A -- Host safety invariants:
- Runs on: macOS + Linux
- Command: `tests/test_unit_host.sh`
- Focus: HVAC on at boot, debounce ambiguity, backward time, queue overflow

Layer 0B -- Zephyr safety invariants:
- Runs on: Linux only (native_posix)
- Command: `tests/test_zephyr_linux.sh`
- Focus: same as 0A under Zephyr integration

Layer 0C -- HIL safety invariants:
- Runs on: device + RTT/UART
- Command: `tests/test_hil_gpio.sh`
- Focus: EV OFF when HVAC asserted; no allow during ambiguity

### Unit tests (host)
- Focus: debounce, edge detection, telemetry payload formatting, no-spam behavior.
- Command:
  - `tests/test_unit_host.sh`

### Zephyr integration tests (native_posix)
- Focus: Zephyr build + ztest execution for host integration.
- Command:
  - `tests/test_zephyr_linux.sh`
- Note: Runs only on Linux (native_posix is not supported on macOS/Windows).

### HIL tests (device + RTT)
- Focus: deterministic GPIO events + Sidewalk send logs.
- Command:
  - `tests/test_hil_gpio.sh`
- Modes:
  - `HIL_MODE=safety` to enforce asserted input (no deasserted events)
  - `HIL_MODE=signal` to validate loopback transition counts
- Expected output (RTT):
  - `GPIO event: hvac state=<0/1> edge=<rising|falling> uptime_ms=<...>`
  - `Sidewalk send: ok 0`

### End-to-end AWS verification
- Focus: payloads arriving on IoT Core topic `sidewalk/app_data`.
- Command:
  - `tests/test_e2e_sidewalk.sh`
- Requirements:
  - AWS credentials configured (`AWS_PROFILE` or default creds).
  - `awsiotsdk` installed:
    - `python3 -m pip install awsiotsdk`
- Optional: `AWS_REGION` (defaults to `us-east-1`), `AWS_IOT_ENDPOINT` to override endpoint lookup.
  - Payloads include `schema_version`, `device_id`, `device_type`, `timestamp`, and `data.gpio`.
  - `timestamp` uses uptime until a time sync downlink is received.
  - DynamoDB verification is done via `tests/e2e_verify_dynamodb.py`.
  - Idempotency check uses `tests/e2e_idempotency_check.sh`.

### Time sync (optional)
- Send a downlink payload: `{"cmd":"time_sync","epoch_ms":1704067200000}`
- Device computes `epoch_at_boot_ms` and switches `timestamp` to epoch ms.
- Note: CLI commands are not available (CLI sources removed).

### EVSE sampling (optional)
- Enable `CONFIG_SID_END_DEVICE_EVSE_ENABLED` and set GPIO/ADC mappings in Kconfig.
- EVSE payloads are sent on pilot/proximity state changes.
- Note: CLI commands are not available (CLI sources removed).

### Line current monitoring (optional)
- Enable `CONFIG_SID_END_DEVICE_LINE_CURRENT_ENABLED`.
- Set `CONFIG_SID_END_DEVICE_LINE_CURRENT_ADC_CHANNEL` for the clamp input.
- Calibrate `CONFIG_SID_END_DEVICE_LINE_CURRENT_SCALE_NUM` and
  `CONFIG_SID_END_DEVICE_LINE_CURRENT_SCALE_DEN` (mA per mV).
- Adjust `CONFIG_SID_END_DEVICE_LINE_CURRENT_DELTA_MA` to define
  a "significant" change threshold.
- Set `CONFIG_SID_END_DEVICE_LINE_CURRENT_SAMPLE_INTERVAL_MS` to control
  sampling cadence (telemetry only emits on change).

### EVSE bring-up checklist
- TODO: Calibrate `EVSE_PILOT_SCALE_*` and `EVSE_PILOT_BIAS_MV`.
- TODO: Calibrate `EVSE_CURRENT_SCALE_*` for your current sensor.
- TODO: Consider upgrading PWM capture to true timer input capture if needed.
- TODO: Validate PWM duty accuracy against a scope.
- Step: Build/flash the firmware and validate raw readings.

### Remaining project TODOs
- TODO: Seed `device_config` table with real device parameters.
- TODO: Validate end-to-end with the real Sidewalk device (not just test publishes).

## Execution Guidelines

- Every commit (local): `tests/test_unit_host.sh`
- Every PR (CI/Linux): `tests/test_zephyr_linux.sh`
- Before release/hardware change: `tests/test_hil_gpio.sh`
- Before demo/deployment: `tests/test_e2e_sidewalk.sh`

## Schema and Idempotency

Required payload fields (current JSON):
- `schema_version`, `device_id`, `timestamp`, `data.gpio`
- `event_id` when available; duplicates must be rejected
- `time_anomaly` when backward time was clamped

DynamoDB idempotency:
- Partition key = device_id, sort key = event_id
- Conditional write must reject duplicate event_id

## Timestamp Handling

- Before time sync: `timestamp = uptime_ms`
- After time sync: `timestamp = epoch_ms`
- Backward time: clamp to last emitted, set `time_anomaly=true`, EV OFF

## Test-to-Requirement Mapping (Lightweight)

| Requirement | Host Test(s) | Zephyr Test(s) | HIL Test(s) |
| --- | --- | --- | --- |
| HVAC asserted => EV OFF | safety_gate host tests | safety_gate ztests | test_hil_gpio.sh (HVAC held ON) |
| Ambiguity => EV OFF | host safety tests | safety_gate ztests | test_hil_gpio.sh |
| Boot/reset => EV OFF | safety_gate host tests | safety_gate ztests | test_hil_gpio.sh |

## How to run each tier

### Unit
```
tests/test_unit_host.sh
```

### Zephyr integration (Linux)
```
tests/test_zephyr_linux.sh
```

### HIL
```
tests/test_hil_gpio.sh
```

### E2E
```
tests/test_e2e_sidewalk.sh
```

If E2E fails due to permissions, ensure you can run:
```
aws iot describe-endpoint --endpoint-type iot:Data-ATS --region us-east-1
```

Note: E2E requires hardware + Sidewalk connectivity; when hardware is not available,
skip the live run and rely on code review plus unit/Zephyr tests.

## Where to look when failures happen
- Unit tests: `build-tests/host/`.
- Zephyr tests: `build-tests/telemetry/gpio_event/zephyr/`, `build-tests/telemetry/telemetry/zephyr/`.
- HIL logs: `build/hil_gpio_rtt.log`.
- E2E payload capture: `build/e2e_payload.json`.
