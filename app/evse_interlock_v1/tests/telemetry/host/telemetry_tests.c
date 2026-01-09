/*
 * [TEST] Host telemetry schema/golden tests.
 * [TELEMETRY] Verifies schema_version and fixture fidelity.
 */
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include "telemetry/telemetry_gpio.h"
#include "telemetry/telemetry_evse.h"
#include "telemetry/telemetry_line_current.h"

#ifndef TEST_FIXTURES_DIR
#define TEST_FIXTURES_DIR "app/evse_interlock_v1/tests/telemetry/host/fixtures"
#endif

static void read_fixture(const char *name, char *buf, size_t buf_len)
{
	/* [BOILERPLATE] File IO helper for golden fixtures. */
	char path[512];
	FILE *fp;
	size_t read_len;

	snprintf(path, sizeof(path), "%s/%s", TEST_FIXTURES_DIR, name);
	fp = fopen(path, "rb");
	assert(fp != NULL);
	read_len = fread(buf, 1, buf_len - 1, fp);
	while (read_len > 0 && (buf[read_len - 1] == '\n' || buf[read_len - 1] == '\r')) {
		read_len--;
	}
	buf[read_len] = '\0';
	fclose(fp);
}

void test_telemetry_required_fields(void)
{
	/* [TELEMETRY] Required fields and time_anomaly flag. */
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

	int len = telemetry_build_gpio_payload_ex(buf, sizeof(buf), "dev123", "evse",
						  "hvac", 1, GPIO_EDGE_RISING, 1234,
						  NULL, "evt-req-1", true);
	assert(len > 0);
	assert(strstr(buf, "\"schema_version\":\"1.0\"") != NULL);
	assert(strstr(buf, "\"device_id\":\"dev123\"") != NULL);
	assert(strstr(buf, "\"timestamp\":1234") != NULL);
	assert(strstr(buf, "\"data\":{\"gpio\"") != NULL);
	assert(strstr(buf, "\"time_anomaly\":true") != NULL);
	assert(strstr(buf, "\"event_id\"") != NULL);

	len = telemetry_build_evse_payload_ex(buf, sizeof(buf), "dev123", "evse", 9876, &evt,
					      "evt-req-2", false);
	assert(len > 0);
	assert(strstr(buf, "\"schema_version\":\"1.0\"") != NULL);
	assert(strstr(buf, "\"device_id\":\"dev123\"") != NULL);
	assert(strstr(buf, "\"timestamp\":9876") != NULL);
	assert(strstr(buf, "\"data\":{\"evse\"") != NULL);
	assert(strstr(buf, "\"event_id\":\"evt-req-2\"") != NULL);

	struct line_current_event line_evt = {
		.send = true,
		.current_a = 12.345f,
		.event_type = "current_change",
	};
	len = telemetry_build_line_current_payload_ex(buf, sizeof(buf), "dev123", "evse",
						      1111, &line_evt, "evt-req-3", true);
	assert(len > 0);
	assert(strstr(buf, "\"schema_version\":\"1.0\"") != NULL);
	assert(strstr(buf, "\"timestamp\":1111") != NULL);
	assert(strstr(buf, "\"time_anomaly\":true") != NULL);
	assert(strstr(buf, "\"data\":{\"line_current\"") != NULL);
	assert(strstr(buf, "\"event_id\":\"evt-req-3\"") != NULL);
}

void test_telemetry_golden_fixtures(void)
{
	/* [TELEMETRY] Exact JSON formatting for known fixtures. */
	char expected[512];
	char actual[512];
	int len;

	read_fixture("telemetry_gpio_uptime.json", expected, sizeof(expected));
	len = telemetry_build_gpio_payload_ex(actual, sizeof(actual), "dev123", "evse",
					      "hvac", 1, GPIO_EDGE_RISING, 1234,
					      NULL, "evt-uptime", false);
	assert(len > 0);
	assert(strcmp(actual, expected) == 0);

	read_fixture("telemetry_gpio_epoch.json", expected, sizeof(expected));
	len = telemetry_build_gpio_payload_ex(actual, sizeof(actual), "dev123", "evse",
					      "hvac", 1, GPIO_EDGE_RISING,
					      1704067200000LL, NULL, "evt-epoch",
					      false);
	assert(len > 0);
	assert(strcmp(actual, expected) == 0);
}
