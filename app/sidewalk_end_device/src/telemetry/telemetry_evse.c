/*
 * Telemetry payload builder for EVSE events
 */
#include "telemetry/telemetry_evse.h"

#include <stdio.h>

static char telemetry_pilot_state_to_char(enum evse_pilot_state state)
{
	switch (state) {
	case EVSE_PILOT_A:
		return 'A';
	case EVSE_PILOT_B:
		return 'B';
	case EVSE_PILOT_C:
		return 'C';
	case EVSE_PILOT_D:
		return 'D';
	case EVSE_PILOT_E:
		return 'E';
	case EVSE_PILOT_F:
		return 'F';
	default:
		return '?';
	}
}

int telemetry_build_evse_payload(char *buf, size_t buf_len, const char *device_id,
				 const char *device_type, int64_t timestamp_ms,
				 const struct evse_event *evt)
{
	if (!buf || buf_len == 0 || !device_id || !device_type || !evt) {
		return -1;
	}

	int len = snprintf(
		buf, buf_len,
		"{\"schema_version\":\"1.0\",\"device_id\":\"%s\",\"device_type\":\"%s\","
		"\"timestamp\":%lld,\"event_type\":\"%s\",\"location\":null,\"run_id\":null,"
		"\"data\":{\"evse\":{\"pilot_state\":\"%c\",\"pwm_duty_cycle\":%.2f,"
		"\"current_draw\":%.3f,\"proximity_detected\":%s,\"session_id\":\"%s\","
		"\"energy_delivered_kwh\":%.4f}}}",
		device_id, device_type, (long long)timestamp_ms, evt->event_type,
		telemetry_pilot_state_to_char(evt->pilot_state), (double)evt->pwm_duty_cycle,
		(double)evt->current_draw_a, evt->proximity_detected ? "true" : "false",
		evt->session_id ? evt->session_id : "", (double)evt->energy_kwh);

	if (len < 0 || (size_t)len >= buf_len) {
		return -1;
	}
	return len;
}
