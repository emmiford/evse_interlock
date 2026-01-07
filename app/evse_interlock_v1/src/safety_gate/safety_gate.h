/*
 * [EVSE-LOGIC] EVSE interlock safety gate API and fault model.
 * [BOILERPLATE] Uses generic debounce state from gpio_event for input conditioning.
 * Safety contract: any ambiguity or fault forces ev_allowed=false.
 */
#ifndef SAFETY_GATE_H
#define SAFETY_GATE_H

#include <stdbool.h>
#include <stdint.h>

#include "telemetry/gpio_event.h"

#define SAFETY_GATE_MAX_DEBOUNCE_MS 60000

enum safety_fault_flags {
	SAFETY_FAULT_NONE = 0,
	SAFETY_FAULT_AC_UNKNOWN = 1 << 0,
	SAFETY_FAULT_DEBOUNCE_INVALID = 1 << 1,
	SAFETY_FAULT_TIMESTAMP_BACKWARD = 1 << 2,
	SAFETY_FAULT_QUEUE_OVERFLOW = 1 << 3,
	SAFETY_FAULT_INVALID_INPUT = 1 << 4,
};

/* [EVSE-LOGIC] Single authoritative safety gate for EV allow/deny decisions. */
struct safety_gate {
	struct gpio_event_state ac_state;
	bool ev_allowed;
	bool time_anomaly;
	bool debounce_valid;
	uint32_t fault_flags;
	int64_t last_timestamp_ms;
};

void safety_gate_init(struct safety_gate *gate, int64_t debounce_ms);
void safety_gate_update_ac(struct safety_gate *gate, int ac_state, int64_t now_ms);
int64_t safety_gate_apply_timestamp(struct safety_gate *gate, int64_t timestamp_ms);
void safety_gate_set_queue_overflow(struct safety_gate *gate);
bool safety_gate_is_ev_allowed(const struct safety_gate *gate);
bool safety_gate_has_fault(const struct safety_gate *gate, uint32_t flag);
bool safety_gate_time_anomaly(const struct safety_gate *gate);

#endif /* SAFETY_GATE_H */
