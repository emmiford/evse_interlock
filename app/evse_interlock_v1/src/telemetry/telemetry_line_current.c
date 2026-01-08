/*
 * [TELEMETRY] Line current payload builder and schema formatting.
 */
#include "telemetry/telemetry_line_current.h"

#include <stdio.h>

int telemetry_build_line_current_payload(char *buf, size_t buf_len, const char *device_id,
					 const char *device_type, int64_t timestamp_ms,
					 const struct line_current_event *evt,
					 const char *event_id)
{
	return telemetry_build_line_current_payload_ex(buf, buf_len, device_id, device_type,
						       timestamp_ms, evt, event_id, false);
}

int telemetry_build_line_current_payload_ex(char *buf, size_t buf_len, const char *device_id,
					    const char *device_type, int64_t timestamp_ms,
					    const struct line_current_event *evt,
					    const char *event_id, bool time_anomaly)
{
	if (!buf || buf_len == 0 || !device_id || !device_type || !evt || !event_id ||
	    event_id[0] == '\0') {
		return -1;
	}

	int len = snprintf(
		buf, buf_len,
		"{\"schema_version\":\"1.0\",\"device_id\":\"%s\",\"device_type\":\"%s\","
		"\"timestamp\":%lld,\"event_id\":\"%s\",\"time_anomaly\":%s,\"event_type\":\"%s\","
		"\"location\":null,\"run_id\":null,"
		"\"data\":{\"line_current\":{\"current_a\":%.3f}}}",
		device_id, device_type, (long long)timestamp_ms, event_id,
		time_anomaly ? "true" : "false", evt->event_type, (double)evt->current_a);

	if (len < 0 || (size_t)len >= buf_len) {
		return -1;
	}
	return len;
}
