/*
 * [TEST] Host unit tests (no Zephyr).
 * [EVSE-LOGIC] Safety gate invariants and time_sync behavior.
 * [TELEMETRY] Payload schema sanity checks.
 */
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include "telemetry/gpio_event.h"
#include "safety_gate/safety_gate.h"
#include "telemetry/telemetry_gpio.h"
#include "telemetry/telemetry_evse.h"
#include "sidewalk/time_sync.h"

void test_telemetry_required_fields(void);
void test_telemetry_golden_fixtures(void);
static void test_gpio_debounce(void)
{
	/* [BOILERPLATE] Generic debounce behavior; not EVSE-specific. */
	struct gpio_event_state st;
	bool changed = false;

	gpio_event_init(&st, 50);
	assert(gpio_event_update(&st, 0, 0, &changed) == GPIO_EDGE_NONE);
	assert(!changed);
	assert(gpio_event_update(&st, 1, 10, &changed) == GPIO_EDGE_NONE);
	assert(!changed);
	assert(gpio_event_update(&st, 1, 40, &changed) == GPIO_EDGE_NONE);
	assert(!changed);
	assert(gpio_event_update(&st, 1, 70, &changed) == GPIO_EDGE_RISING);
	assert(changed);
}

static void test_gpio_payloads(void)
{
	/* [TELEMETRY] Schema fields for GPIO event payloads. */
	char buf[384];
	int len = telemetry_build_gpio_payload(buf, sizeof(buf), "dev123", "evse",
					       "extinput0", 1, GPIO_EDGE_RISING, 1234,
					       "abcd1234", "evt-1");
	assert(len > 0);
	assert(strstr(buf, "\"edge\":\"rising\"") != NULL);
	assert(strstr(buf, "\"run_id\":\"abcd1234\"") != NULL);

	len = telemetry_build_gpio_payload(buf, sizeof(buf), "dev123", "evse",
					   "extinput0", 0, GPIO_EDGE_FALLING, 4321, NULL,
					   "evt-2");
	assert(len > 0);
	assert(strstr(buf, "\"edge\":\"falling\"") != NULL);
	assert(strstr(buf, "\"run_id\":null") != NULL);
}

static void test_evse_payload(void)
{
	/* [TELEMETRY] EVSE payload fields match schema. */
	char buf[384];
	struct evse_event evt = {
		.send = true,
		.pilot_state = EVSE_PILOT_B,
		.proximity_detected = true,
		.pwm_duty_cycle = 12.5f,
		.current_draw_a = 1.234f,
		.energy_kwh = 0.4567f,
		.event_type = "state_change",
		.session_id = "session-1",
	};

	int len = telemetry_build_evse_payload(buf, sizeof(buf), "dev123", "evse", 9876, &evt,
					       "evt-3");
	assert(len > 0);
	assert(strstr(buf, "\"pilot_state\":\"B\"") != NULL);
	assert(strstr(buf, "\"pwm_duty_cycle\":12.50") != NULL);
	assert(strstr(buf, "\"current_draw\":1.234") != NULL);
	assert(strstr(buf, "\"proximity_detected\":true") != NULL);
	assert(strstr(buf, "\"session_id\":\"session-1\"") != NULL);
	assert(strstr(buf, "\"energy_delivered_kwh\":0.4567") != NULL);
}

static void test_safety_ac_on_at_boot(void)
{
	/* [EVSE-LOGIC] AC asserted at boot => EV OFF. */
	struct safety_gate gate;

	safety_gate_init(&gate, 50);
	safety_gate_update_ac(&gate, 1, 0);
	assert(!safety_gate_is_ev_allowed(&gate));
}

static void test_safety_ac_toggle_debounce(void)
{
	/* [EVSE-LOGIC] Debounce ambiguity => EV OFF. */
	struct safety_gate gate;

	safety_gate_init(&gate, 50);
	safety_gate_update_ac(&gate, 0, 0);
	safety_gate_update_ac(&gate, 1, 10);
	assert(!safety_gate_is_ev_allowed(&gate));
	safety_gate_update_ac(&gate, 0, 20);
	assert(!safety_gate_is_ev_allowed(&gate));
}

static void test_safety_ac_unknown(void)
{
	/* [EVSE-LOGIC] Unknown input => EV OFF + fault. */
	struct safety_gate gate;

	safety_gate_init(&gate, 50);
	safety_gate_update_ac(&gate, -1, 10);
	assert(!safety_gate_is_ev_allowed(&gate));
	assert(safety_gate_has_fault(&gate, SAFETY_FAULT_AC_UNKNOWN));
}

static void test_safety_timestamp_backward(void)
{
	/* [EVSE-LOGIC] Backward time clamps and disables EV. */
	struct safety_gate gate;
	int64_t ts;

	safety_gate_init(&gate, 50);
	(void)safety_gate_apply_timestamp(&gate, 1000);
	ts = safety_gate_apply_timestamp(&gate, 900);
	assert(ts == 1000);
	assert(!safety_gate_is_ev_allowed(&gate));
	assert(safety_gate_has_fault(&gate, SAFETY_FAULT_TIMESTAMP_BACKWARD));
	assert(safety_gate_time_anomaly(&gate));
}

static void test_safety_queue_overflow(void)
{
	/* [EVSE-LOGIC] Queue overflow is treated as ambiguity. */
	struct safety_gate gate;

	safety_gate_init(&gate, 50);
	safety_gate_set_queue_overflow(&gate);
	assert(!safety_gate_is_ev_allowed(&gate));
	assert(safety_gate_has_fault(&gate, SAFETY_FAULT_QUEUE_OVERFLOW));
}

static void test_safety_no_time_sync(void)
{
	/* [EVSE-LOGIC] No sync => EV OFF until stable state is proven. */
	struct safety_gate gate;

	safety_gate_init(&gate, 50);
	(void)safety_gate_apply_timestamp(&gate, 1234);
	assert(!safety_gate_is_ev_allowed(&gate));
	assert(!safety_gate_time_anomaly(&gate));
}

static void test_safety_invalid_debounce(void)
{
	/* [EVSE-LOGIC] Invalid debounce config forces EV OFF. */
	struct safety_gate gate;

	safety_gate_init(&gate, 0);
	assert(!safety_gate_is_ev_allowed(&gate));
	assert(safety_gate_has_fault(&gate, SAFETY_FAULT_DEBOUNCE_INVALID));
	safety_gate_update_ac(&gate, 0, 0);
	assert(!safety_gate_is_ev_allowed(&gate));
}

static void test_safety_null_pointers(void)
{
	/* [BOILERPLATE] Defensive null handling. */
	struct safety_gate gate;

	safety_gate_init(NULL, 50);
	safety_gate_update_ac(NULL, 1, 0);
	(void)safety_gate_apply_timestamp(NULL, 100);
	safety_gate_set_queue_overflow(NULL);

	safety_gate_init(&gate, 50);
	safety_gate_update_ac(&gate, -1, 0);
	assert(!safety_gate_is_ev_allowed(&gate));
	assert(safety_gate_has_fault(&gate, SAFETY_FAULT_INVALID_INPUT));
}

static void test_time_sync_backward_clamp(void)
{
	/* [TELEMETRY] Time sync clamps backward epoch updates. */
	int64_t ts;

	time_sync_init();
	ts = time_sync_get_timestamp_ms(1000);
	assert(ts == 1000);
	assert(!time_sync_time_anomaly());

	time_sync_apply_epoch_ms(500, 1000);
	ts = time_sync_get_timestamp_ms(1100);
	assert(ts == 1100);
	assert(time_sync_time_anomaly());

	ts = time_sync_get_timestamp_ms(1200);
	assert(ts == 1200);
}

int main(void)
{
	test_gpio_debounce();
	test_gpio_payloads();
	test_evse_payload();
	test_safety_ac_on_at_boot();
	test_safety_ac_toggle_debounce();
	test_safety_ac_unknown();
	test_safety_timestamp_backward();
	test_safety_queue_overflow();
	test_safety_no_time_sync();
	test_safety_invalid_debounce();
	test_safety_null_pointers();
	test_time_sync_backward_clamp();
	test_telemetry_required_fields();
	test_telemetry_golden_fixtures();
	return 0;
}
