# Sidewalk GPIO Tests

## What input triggers E2E?
- Trigger: simulator mode (no readable on-board button on RAK4631).
- Alias: `extinput0` (defined in `app/evse_interlock_v1/boards/rak4631.overlay`).
- Active level: active low (pull-up enabled in overlay).
- Events per press: 2 (rising + falling) when using a real input; simulator toggles both edges.
- E2E default: simulator mode with a unique `run_id` printed to UART and embedded in payloads.

Note: RAK4631 DTS has no `gpio-keys`/`button0` aliases; reset is wired to nRESET and is not a readable GPIO.

## Test Plan (Pragmatic)

| Test Type | Runs On | Purpose |
|---------|--------|--------|
| Unit | macOS/Linux | Logic correctness |
| Zephyr Integration | Linux only | OS + drivers |
| E2E | Hardware | System validation |

### Unit tests (host)
- Focus: debounce, edge detection, telemetry payload formatting, no-spam behavior.
- Command:
  - `tests/test_unit_host.sh`

### Zephyr integration tests (native_posix)
- Focus: Zephyr build + ztest execution for host integration.
- Command:
  - `tests/test_zephyr_linux.sh`

### HIL tests (device + RTT)
- Focus: deterministic GPIO events + Sidewalk send logs.
- Command:
  - `tests/test_hil_gpio.sh`
- Modes:
  - `HIL_MODE=safety` to enforce asserted input (no deasserted events)
  - `HIL_MODE=signal` to validate loopback transition counts
- Expected output (RTT):
  - `E2E run_id: <id>`
  - `GPIO event: extinput0 state=<0/1> edge=<rising|falling> uptime_ms=<...>`
  - `Sidewalk send: ok 0`

### End-to-end AWS verification
- Focus: payloads arriving on IoT Core topic `sidewalk/#`.
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
- CLI alternative: `sid time_set <epoch_ms>` (if CLI enabled).

### EVSE sampling (optional)
- Enable `CONFIG_SID_END_DEVICE_EVSE_ENABLED` and set GPIO/ADC mappings in Kconfig.
- EVSE payloads are sent on pilot/proximity state changes.
- CLI: `sid evse_read` prints pilot mv/state, duty cycle, current, and proximity.

### EVSE bring-up checklist
- TODO: Calibrate `EVSE_PILOT_SCALE_*` and `EVSE_PILOT_BIAS_MV`.
- TODO: Calibrate `EVSE_CURRENT_SCALE_*` for your current sensor.
- TODO: Consider upgrading PWM capture to true timer input capture if needed.
- TODO: Validate PWM duty accuracy against a scope.
- Step: Build/flash the firmware and validate raw readings.

### Remaining project TODOs
- TODO: Seed `device_config` table with real device parameters.
- TODO: Validate end-to-end with the real Sidewalk device (not just test publishes).

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
- E2E logs: `build/e2e_rtt.log`.
