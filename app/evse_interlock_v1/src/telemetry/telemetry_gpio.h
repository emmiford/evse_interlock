/*
 * Telemetry payload builder for GPIO events
 */
#ifndef TELEMETRY_GPIO_H
#define TELEMETRY_GPIO_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "telemetry/gpio_event.h"

#define TELEMETRY_EVENT_ID_SUPPORTED 1

int telemetry_build_gpio_payload(char *buf, size_t buf_len, const char *device_id,
				 const char *device_type, const char *pin_alias, int state,
				 gpio_edge_t edge, int64_t uptime_ms, const char *run_id,
				 const char *event_id);

int telemetry_build_gpio_payload_ex(char *buf, size_t buf_len, const char *device_id,
				    const char *device_type, const char *pin_alias, int state,
				    gpio_edge_t edge, int64_t uptime_ms, const char *run_id,
				    const char *event_id, bool time_anomaly);

#endif /* TELEMETRY_GPIO_H */
