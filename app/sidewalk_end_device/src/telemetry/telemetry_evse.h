/*
 * Telemetry payload builder for EVSE events
 */
#ifndef TELEMETRY_EVSE_H
#define TELEMETRY_EVSE_H

#include <stddef.h>
#include <stdint.h>

#include "evse.h"

int telemetry_build_evse_payload(char *buf, size_t buf_len, const char *device_id,
				 const char *device_type, int64_t timestamp_ms,
				 const struct evse_event *evt);

#endif /* TELEMETRY_EVSE_H */
