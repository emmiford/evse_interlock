/*
 * [TELEMETRY] Line current payload builder.
 */
#ifndef TELEMETRY_LINE_CURRENT_H
#define TELEMETRY_LINE_CURRENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "telemetry/line_current.h"

int telemetry_build_line_current_payload(char *buf, size_t buf_len, const char *device_id,
					 const char *device_type, int64_t timestamp_ms,
					 const struct line_current_event *evt,
					 const char *event_id);
int telemetry_build_line_current_payload_ex(char *buf, size_t buf_len, const char *device_id,
					    const char *device_type, int64_t timestamp_ms,
					    const struct line_current_event *evt,
					    const char *event_id, bool time_anomaly);

#endif /* TELEMETRY_LINE_CURRENT_H */
