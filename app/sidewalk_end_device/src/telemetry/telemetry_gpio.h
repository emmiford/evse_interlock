/*
 * Telemetry payload builder for GPIO events
 */
#ifndef TELEMETRY_GPIO_H
#define TELEMETRY_GPIO_H

#include <stddef.h>
#include <stdint.h>

#include "gpio_event.h"

int telemetry_build_gpio_payload(char *buf, size_t buf_len, const char *device_id,
				 const char *device_type, const char *pin_alias, int state,
				 gpio_edge_t edge, int64_t uptime_ms, const char *run_id);

#endif /* TELEMETRY_GPIO_H */
