/*
 * Telemetry payload unit tests (host)
 */
#include <zephyr/ztest.h>
#include <string.h>

#include "telemetry/telemetry_gpio.h"
#include "telemetry/telemetry_evse.h"

ZTEST(telemetry, test_gpio_payload_rising)
{
	char buf[384];
	int len = telemetry_build_gpio_payload(buf, sizeof(buf), "dev123", "evse",
					       "extinput0", 1, GPIO_EDGE_RISING, 1234,
					       "abcd1234", "evt-1");
	zassert_true(len > 0, NULL);
	zassert_not_null(strstr(buf, "\"schema_version\":\"1.0\""), NULL);
	zassert_not_null(strstr(buf, "\"device_id\":\"dev123\""), NULL);
	zassert_not_null(strstr(buf, "\"device_type\":\"evse\""), NULL);
	zassert_not_null(strstr(buf, "\"event_type\":\"state_change\""), NULL);
	zassert_not_null(strstr(buf, "\"event_id\":\"evt-1\""), NULL);
	zassert_not_null(strstr(buf, "\"pin\":\"extinput0\""), NULL);
	zassert_not_null(strstr(buf, "\"state\":1"), NULL);
	zassert_not_null(strstr(buf, "\"edge\":\"rising\""), NULL);
	zassert_not_null(strstr(buf, "\"run_id\":\"abcd1234\""), NULL);
}

ZTEST(telemetry, test_gpio_payload_falling)
{
	char buf[384];
	int len = telemetry_build_gpio_payload(buf, sizeof(buf), "dev123", "evse",
					       "extinput0", 0, GPIO_EDGE_FALLING, 4321,
					       NULL, "evt-2");
	zassert_true(len > 0, NULL);
	zassert_not_null(strstr(buf, "\"edge\":\"falling\""), NULL);
	zassert_not_null(strstr(buf, "\"run_id\":null"), NULL);
}

ZTEST(telemetry, test_evse_payload_fields)
{
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
	zassert_true(len > 0, NULL);
	zassert_not_null(strstr(buf, "\"schema_version\":\"1.0\""), NULL);
	zassert_not_null(strstr(buf, "\"event_type\":\"state_change\""), NULL);
	zassert_not_null(strstr(buf, "\"event_id\":\"evt-3\""), NULL);
	zassert_not_null(strstr(buf, "\"pilot_state\":\"B\""), NULL);
	zassert_not_null(strstr(buf, "\"pwm_duty_cycle\":12.50"), NULL);
	zassert_not_null(strstr(buf, "\"current_draw\":1.234"), NULL);
	zassert_not_null(strstr(buf, "\"proximity_detected\":true"), NULL);
	zassert_not_null(strstr(buf, "\"session_id\":\"session-1\""), NULL);
	zassert_not_null(strstr(buf, "\"energy_delivered_kwh\":0.4567"), NULL);
}

ZTEST_SUITE(telemetry, NULL, NULL, NULL, NULL, NULL);
