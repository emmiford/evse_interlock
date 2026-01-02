# Sidewalk GPIO Tests

## What input triggers E2E?
- Trigger: simulator mode (no readable on-board button on RAK4631).
- Alias: `extinput0` (defined in `app/sidewalk_end_device/boards/rak4631.overlay`).
- Active level: active low (pull-up enabled in overlay).
- Events per press: 2 (rising + falling) when using a real input; simulator toggles both edges.
- E2E default: simulator mode with a unique `run_id` printed to UART and embedded in payloads.

Note: RAK4631 DTS has no `gpio-keys`/`button0` aliases; reset is wired to nRESET and is not a readable GPIO.

## Test Plan (Pragmatic)

### Unit tests (host)
- Focus: debounce, edge detection, payload formatting, no-spam behavior.
- Command:
  - `tools/test_unit.sh`

### HIL tests (device + RTT)
- Focus: deterministic GPIO events + Sidewalk send logs.
- Command:
  - `tools/test_hil_gpio.sh`
- Expected output (RTT):
  - `E2E run_id: <id>`
  - `GPIO event: extinput0 state=<0/1> edge=<rising|falling> uptime_ms=<...>`
  - `Sidewalk send: ok 0`

### End-to-end AWS verification
- Focus: payloads arriving on IoT Core topic `sidewalk/app_data`.
- Command:
  - `tools/test_e2e_sidewalk.sh`
- Requirements:
  - AWS credentials configured (`AWS_PROFILE` or default creds).
  - `awsiotsdk` installed:
    - `python3 -m pip install awsiotsdk`
  - Optional: `AWS_REGION` (defaults to `us-east-1`), `AWS_IOT_ENDPOINT` to override endpoint lookup.

## How to run each tier

### Unit
```
tools/test_unit.sh
```

### HIL
```
tools/test_hil_gpio.sh
```

### E2E
```
tools/test_e2e_sidewalk.sh
```

If E2E fails due to permissions, ensure you can run:
```
aws iot describe-endpoint --endpoint-type iot:Data-ATS --region us-east-1
```

## Where to look when failures happen
- Unit tests: `build-tests/gpio_event/zephyr/`.
- HIL logs: `build/hil_gpio_rtt.log`.
- E2E logs: `build/e2e_rtt.log`.
