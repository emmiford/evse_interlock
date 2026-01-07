/*
 * [EVSE-LOGIC] Single authoritative EVSE safety gate for allow/deny decisions.
 * [BOILERPLATE] Uses gpio_event debounce helper (typical Zephyr-style input conditioning).
 * Safety contract: AC asserted/unknown, invalid debounce, timestamp anomalies, or queue
 * overflow => EV OFF.
 */
#include "safety_gate/safety_gate.h"

/* BEGIN PROJECT CODE: EVSE interlock safety policy (no third-party logic below). */

static bool debounce_valid(int64_t debounce_ms)
{
	return debounce_ms > 0 && debounce_ms <= SAFETY_GATE_MAX_DEBOUNCE_MS;
}

/* [BOILERPLATE] Init typical debounce + fault tracking state. */
void safety_gate_init(struct safety_gate *gate, int64_t debounce_ms)
{
	if (!gate) {
		return;
	}

	gate->ev_allowed = false;
	gate->time_anomaly = false;
	gate->fault_flags = SAFETY_FAULT_NONE;
	gate->last_timestamp_ms = -1;
	gate->debounce_valid = debounce_valid(debounce_ms);

	if (!gate->debounce_valid) {
		gate->fault_flags |= SAFETY_FAULT_DEBOUNCE_INVALID;
	}

	gpio_event_init(&gate->ac_state, gate->debounce_valid ? debounce_ms : 0);
}

/*
 * [EVSE-LOGIC] AUTHORITATIVE SAFETY GATE:
 * AC asserted/unknown or any ambiguity => EV OFF. Only a stable, debounced AC OFF
 * allows EV ON.
 */
void safety_gate_update_ac(struct safety_gate *gate, int ac_state, int64_t now_ms)
{
	if (!gate) {
		return;
	}

	if (!gate->debounce_valid) {
		gate->ev_allowed = false;
		return;
	}

	if (ac_state < 0) {
		gate->fault_flags |= SAFETY_FAULT_AC_UNKNOWN | SAFETY_FAULT_INVALID_INPUT;
		gate->ev_allowed = false;
		return;
	}

	(void)gpio_event_update(&gate->ac_state, ac_state, now_ms, NULL);

	/* [EVSE-LOGIC] "Stable OFF" is the only permissive state; anything else is unsafe. */
	bool stable_off = gate->ac_state.initialized &&
			  gate->ac_state.pending_state == 0 &&
			  gate->ac_state.last_state == 0 &&
			  (now_ms - gate->ac_state.last_change_ms) >=
				  gate->ac_state.debounce_ms;

	if (gate->fault_flags != SAFETY_FAULT_NONE || !stable_off) {
		gate->ev_allowed = false;
		return;
	}

	gate->ev_allowed = true;
}

/* [EVSE-LOGIC] Timestamp clamp and anomaly flag feed the safety gate. */
int64_t safety_gate_apply_timestamp(struct safety_gate *gate, int64_t timestamp_ms)
{
	if (!gate) {
		return timestamp_ms;
	}

	if (gate->last_timestamp_ms >= 0 && timestamp_ms < gate->last_timestamp_ms) {
		gate->fault_flags |= SAFETY_FAULT_TIMESTAMP_BACKWARD;
		gate->time_anomaly = true;
		gate->ev_allowed = false;
		return gate->last_timestamp_ms;
	}

	gate->last_timestamp_ms = timestamp_ms;
	return timestamp_ms;
}

/* [EVSE-LOGIC] Queue overflow is treated as ambiguity -> EV OFF. */
void safety_gate_set_queue_overflow(struct safety_gate *gate)
{
	if (!gate) {
		return;
	}

	gate->fault_flags |= SAFETY_FAULT_QUEUE_OVERFLOW;
	gate->ev_allowed = false;
}

bool safety_gate_is_ev_allowed(const struct safety_gate *gate)
{
	return gate ? gate->ev_allowed : false;
}

bool safety_gate_has_fault(const struct safety_gate *gate, uint32_t flag)
{
	return gate ? (gate->fault_flags & flag) != 0 : false;
}

bool safety_gate_time_anomaly(const struct safety_gate *gate)
{
	return gate ? gate->time_anomaly : false;
}
