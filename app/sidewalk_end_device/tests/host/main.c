/*
 * Host unit tests (no Zephyr)
 */
#include <assert.h>
#include <string.h>
#include <stdbool.h>

#include "gpio_event.h"
#include "telemetry/telemetry_gpio.h"
#include "telemetry/telemetry_evse.h"

static void test_gpio_debounce(void)
{
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
	char buf[256];
	int len = telemetry_build_gpio_payload(buf, sizeof(buf), "dev123", "evse",
					       "extinput0", 1, GPIO_EDGE_RISING, 1234,
					       "abcd1234");
	assert(len > 0);
	assert(strstr(buf, "\"edge\":\"rising\"") != NULL);
	assert(strstr(buf, "\"run_id\":\"abcd1234\"") != NULL);

	len = telemetry_build_gpio_payload(buf, sizeof(buf), "dev123", "evse",
					   "extinput0", 0, GPIO_EDGE_FALLING, 4321, NULL);
	assert(len > 0);
	assert(strstr(buf, "\"edge\":\"falling\"") != NULL);
	assert(strstr(buf, "\"run_id\":null") != NULL);
}

static void test_evse_payload(void)
{
	char buf[320];
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

	int len = telemetry_build_evse_payload(buf, sizeof(buf), "dev123", "evse", 9876, &evt);
	assert(len > 0);
	assert(strstr(buf, "\"pilot_state\":\"B\"") != NULL);
	assert(strstr(buf, "\"pwm_duty_cycle\":12.50") != NULL);
	assert(strstr(buf, "\"current_draw\":1.234") != NULL);
	assert(strstr(buf, "\"proximity_detected\":true") != NULL);
	assert(strstr(buf, "\"session_id\":\"session-1\"") != NULL);
	assert(strstr(buf, "\"energy_delivered_kwh\":0.4567") != NULL);
}

int main(void)
{
	test_gpio_debounce();
	test_gpio_payloads();
	test_evse_payload();
	return 0;
}
