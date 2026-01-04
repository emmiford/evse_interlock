# EVSE Safety State Machine

## Purpose

Define the safety gating state machine for EV enable control. The rule is
simple and non-negotiable: any ambiguity => EV OFF.

## Inputs

- AC input (logical): from `extinput0` GPIO alias, after Zephyr polarity.
  - `1` means asserted (active), `0` means deasserted (inactive).
- Timestamp (ms): uptime or epoch (after time_sync).
- Debounce config (ms): configured debounce duration.
- Queue overflow flag: indicates GPIO ISR/event queue overflow.

## Outputs

- ev_allowed (bool): false => EV OFF, true => EV allowed.
- fault flags (bitmask): records ambiguity conditions.
- time_anomaly (bool): true when backward time was detected and clamped.

## States

The implementation tracks a small set of derived states:

- AC_UNKNOWN: input not initialized or invalid.
- AC_ON: stable asserted after debounce.
- AC_OFF: stable deasserted after debounce.
- FAULT: any fault flag is set.

The system is always safe-default: EV OFF unless explicitly allowed.

## Transitions

Boot:

- Start in AC_UNKNOWN with ev_allowed=false.
- First sample initializes the debounce tracker but does not allow EV.

AC sampling:

- If input is invalid or unknown -> set fault flag AC_UNKNOWN, ev_allowed=false.
- If input toggles within debounce window -> remain EV OFF.
- If input stabilizes OFF for >= debounce_ms and no faults -> ev_allowed=true.
- If input stabilizes ON -> ev_allowed=false.

Timestamp handling:

- If timestamp < last_timestamp:
  - Clamp to last_timestamp.
  - Set time_anomaly=true.
  - Set fault flag TIMESTAMP_BACKWARD.
  - ev_allowed=false.

Queue overflow:

- Set fault flag QUEUE_OVERFLOW.
- ev_allowed=false.

Debounce config validation:

- If debounce_ms <= 0 or absurdly large:
  - Set fault flag DEBOUNCE_INVALID.
  - ev_allowed=false.

## Ambiguity Cases (Fail-Safe)

Any of the following must force EV OFF:

- AC input unknown/uninitialized
- Debounce config invalid
- Timestamp backward/overflow
- Queue overflow or dropped events
- Any internal fault flag set

## Fault Flags (V1)

- AC_UNKNOWN: invalid or unknown AC input
- DEBOUNCE_INVALID: debounce config out of range
- TIMESTAMP_BACKWARD: backward time detected
- QUEUE_OVERFLOW: ISR/event queue overflow
- INVALID_INPUT: any invalid input detected by safety gate

## Rule of Priority

AC asserted or any ambiguity ALWAYS overrides any other logic. If AC is
asserted or state is ambiguous, ev_allowed must remain false.

