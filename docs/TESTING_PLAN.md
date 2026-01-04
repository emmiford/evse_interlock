# EVSE Interlock Testing Plan

## Overview

This plan defines a complete testing strategy for the evse_interlock firmware and
AWS integration. It builds on existing scripts and adds explicit safety gates,
schema stability checks, and CI wiring to keep safety invariants un-regressable.

Critical priority: safety invariants take precedence over all other concerns.
AC asserted or any ambiguity MUST result in EV OFF.

## Problem Statement

The project requires confidence that:

- Safety-critical behavior: AC asserted -> EV OFF under all conditions
- Ambiguity handling: any unknown or invalid state -> EV OFF
- Logic correctness: debounce, edge detection, telemetry formatting
- Hardware integration: real RAK4630/RAK4631 GPIO events and Sidewalk sends
- Cloud connectivity: Sidewalk -> AWS IoT Core -> DynamoDB delivery
- Schema stability: payload changes do not break consumers

## Safety Invariants (V1) -- Must Never Regress

Hard requirements:

1. AC asserted => EV OFF (no exceptions, ever)
2. Any ambiguity => EV OFF (unknown input, invalid transition, timestamp anomaly,
   missing init)
3. Boot/reset/brownout => EV OFF default
4. Firmware crash => EV OFF still enforced by analog logic (firmware only affects
   telemetry/diagnostics)

Firmware-level definitions:

- EV OFF means: EV enable GPIO output is deasserted (logic low), preventing the
  vehicle charging relay from closing. In firmware logic, this is represented by
  `ev_allowed == false`.
- AC asserted means: the AC input GPIO reads logical high after debounce
  stabilization. On RAK4631, `extinput0` is defined as `GPIO_ACTIVE_LOW`, so
  `gpio_pin_get_dt(&app_gpio) == 1` is the asserted (active) state after the
  Zephyr GPIO API applies polarity.
- ev_allowed is the firmware state flag that gates EV enable output; false means
  EV OFF, true means EV allowed only when AC is confirmed OFF and stable.

Definition of ambiguity (explicit list):

- AC input unknown (not initialized, out-of-range, contradictory, too-fast toggles)
- Event timestamps go backward or overflow handling fails
- Debounce config invalid (negative, zero, or absurdly large)
- GPIO ISR/event queue overflow or drop detected
- Any internal fault flag set (watchdog reset, stack overflow, asserts in dev builds)

Global acceptance criterion:

- No test case in any tier allows EV while AC is asserted or ambiguous.

## Testing Layers

### Layer 0: Safety Gate Tests (Regression Tripwires)

Layer 0A -- Host Safety Invariant Tests:

- Runs on: macOS + Linux (local dev), mandatory in CI
- Command: `tools/test_unit_host.sh`
- Scope:
  - AC=ON at boot => ev_allowed=false
  - AC toggles during debounce window => ev_allowed=false until stable OFF
  - AC input unknown/uninitialized => ev_allowed=false + fault
  - Event timestamps out-of-order => ev_allowed=false + fault + time_anomaly=true
  - Event queue overflow simulated => ev_allowed=false + fault
  - time_sync absent (uptime mode) => no extra fault; safety gate unchanged
  - Invalid debounce (negative/zero/absurd) => ev_allowed=false + fault
  - Null pointer/invalid inputs => ev_allowed=false + fault

Layer 0B -- Zephyr Safety Invariant Tests:

- Runs on: Linux only (CI or VM)
- Command: `tools/test_zephyr_linux.sh` with `BOARD=native_posix`
- Scope: mirror Layer 0A cases under ztest to ensure Zephyr integration cannot
  bypass safety logic.

Layer 0C -- HIL Safety Invariant Verification:

- Runs on: real device + RTT/UART
- Command: `tools/test_hil_gpio.sh`
- Scope:
  - AC input held ON => EV output never enables
  - After reset => EV OFF until AC confirmed OFF and stable
  - No log messages indicate EV allow transition while AC asserted

### 1. Unit Tests (Host)

- Runs on: macOS + Linux (local dev), optional in CI
- Command: `tools/test_unit_host.sh`
- Scope:
  - gpio_event: debounce, edge detection, no-spam, transitions
  - telemetry: payload field correctness, schema stability
- Current implementation: custom test harness in
  `app/sidewalk_end_device/tests/host/main.c`

### 2. Zephyr Integration Tests

- Runs on: Linux only (CI or VM)
- Command: `tools/test_zephyr_linux.sh` with `BOARD=native_posix`
- Scope:
  - ztests in `tests/gpio_event`, `tests/telemetry`, `tests/safety_gate`
  - Kconfig sanity (logging off, minimal heap)

### 3. Hardware-in-the-Loop (HIL)

- Runs on: real device + RTT/UART
- Command: `tools/test_hil_gpio.sh`
- Acceptance checks:
  - run_id appears in RTT logs
  - GPIO toggles => rising/falling edges logged
  - Sidewalk send returns ok

### 4. End-to-End (AWS)

- Runs on: hardware + AWS account
- Command: `tools/test_e2e_sidewalk.sh`
- Acceptance checks:
  - MQTT receives message on `sidewalk/#`
  - Payload includes required fields
  - DynamoDB device_events table receives records
  - TTL in epoch seconds (not milliseconds)

## Technical Requirements

### Test Execution Guidelines

- Every commit (local): `tools/test_unit_host.sh`
- Every PR (CI): `tools/test_zephyr_linux.sh` (Layer 0B included)
- Before release/hardware change: `tools/test_hil_gpio.sh`
- Before demo/deployment: `tools/test_e2e_sidewalk.sh`

### JSON Schema Stability

- Required fields must be present:
  - schema_version, device_id, timestamp, data.{gpio|evse}
  - event_id (if available at this layer)
  - time_anomaly present when backward-time clamp occurs
- Golden fixtures validate exact JSON output for fixed inputs (uptime_ms and
  epoch_ms modes).

### Binary Encoding ABI Checks (Binary-Only)

- Apply only to packed binary payloads, not JSON.
- Use static_asserts for struct sizes/offsets if binary encoding is introduced.

### Timestamp Handling

Two modes:

- Before time_sync: timestamp = uptime_ms
- After time_sync: timestamp = epoch_ms

Backward time handling strategy:

- Clamp backward time to last_timestamp_emitted
- Set fault flag
- Emit telemetry field `time_anomaly=true`

### Idempotency Contract (DynamoDB)

- Each event payload must include `event_id` (UUID or 64-bit unique ID)
- DynamoDB conditional write prevents duplicates:
  - Partition key = device_id, sort key = event_id
  - Duplicate event_id must be rejected (no duplicate rows)

### run_id Format

- 8-character hexadecimal string (e.g., "a1b2c3d4")
- Used for E2E correlation

## Test-to-Requirement Mapping (Lightweight)

| Requirement | Host Test(s) | Zephyr Test(s) | HIL Test(s) |
| --- | --- | --- | --- |
| AC asserted => EV OFF | test_safety_ac_on_at_boot, test_safety_ac_toggle_debounce | safety_gate::test_ac_on_at_boot, safety_gate::test_ac_toggle_debounce | test_hil_gpio.sh (AC held ON) |
| Any ambiguity => EV OFF | test_safety_ac_unknown, test_safety_timestamp_backward, test_safety_invalid_debounce, test_safety_queue_overflow | safety_gate::test_ac_unknown, safety_gate::test_timestamp_backward, safety_gate::test_invalid_debounce, safety_gate::test_queue_overflow | test_hil_gpio.sh (no allow while ambiguous) |
| Boot/reset => EV OFF | test_safety_ac_on_at_boot | safety_gate::test_ac_on_at_boot | test_hil_gpio.sh (after reset) |
| AC priority over all | test_safety_ac_on_at_boot | safety_gate::test_ac_on_at_boot | test_hil_gpio.sh (AC held ON) |

## Out of Scope (V1)

- Performance benchmarking
- Fuzz testing
- Load testing (100+ devices)
- Security testing
- Formal test framework adoption

## Open Questions (Documented)

1. Device model consistency: TESTING.md references RAK4631 vs RAK4630
2. run_id format enforcement vs current behavior
3. DynamoDB write path (IoT Rule/Lambda names)

