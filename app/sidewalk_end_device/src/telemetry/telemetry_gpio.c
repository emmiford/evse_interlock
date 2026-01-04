/*
 * Telemetry payload builder for GPIO events
 */
#include "telemetry/telemetry_gpio.h"

#include <stdio.h>

int telemetry_build_gpio_payload(char *buf, size_t buf_len, const char *device_id,
				 const char *device_type, const char *pin_alias, int state,
				 gpio_edge_t edge, int64_t uptime_ms, const char *run_id)
{
	return telemetry_build_gpio_payload_ex(buf, buf_len, device_id, device_type, pin_alias,
					       state, edge, uptime_ms, run_id, false);
}

int telemetry_build_gpio_payload_ex(char *buf, size_t buf_len, const char *device_id,
				    const char *device_type, const char *pin_alias, int state,
				    gpio_edge_t edge, int64_t uptime_ms, const char *run_id,
				    bool time_anomaly)
{
	if (!buf || buf_len == 0 || !pin_alias || !device_id || !device_type) {
		return -1;
	}

	const char *edge_str = gpio_edge_str(edge);
	int len = 0;
	/* timestamp uses uptime_ms until we have a real epoch source */

	if (run_id && run_id[0] != '\0') {
		len = snprintf(buf, buf_len,
			       "{\"schema_version\":\"1.0\",\"device_id\":\"%s\","
			       "\"device_type\":\"%s\",\"timestamp\":%lld,"
			       "\"time_anomaly\":%s,\"event_type\":\"state_change\","
			       "\"location\":null,"
			       "\"run_id\":\"%s\",\"data\":{\"gpio\":{\"pin\":\"%s\","
			       "\"state\":%d,\"edge\":\"%s\",\"uptime_ms\":%lld}}}",
			       device_id, device_type, (long long)uptime_ms,
			       time_anomaly ? "true" : "false", run_id,
			       pin_alias, state, edge_str, (long long)uptime_ms);
	} else {
		len = snprintf(buf, buf_len,
			       "{\"schema_version\":\"1.0\",\"device_id\":\"%s\","
			       "\"device_type\":\"%s\",\"timestamp\":%lld,"
			       "\"time_anomaly\":%s,\"event_type\":\"state_change\","
			       "\"location\":null,"
			       "\"run_id\":null,\"data\":{\"gpio\":{\"pin\":\"%s\","
			       "\"state\":%d,\"edge\":\"%s\",\"uptime_ms\":%lld}}}",
			       device_id, device_type, (long long)uptime_ms,
			       time_anomaly ? "true" : "false", pin_alias, state, edge_str,
			       (long long)uptime_ms);
	}

	if (len < 0 || (size_t)len >= buf_len) {
		return -1;
	}
	return len;
}
