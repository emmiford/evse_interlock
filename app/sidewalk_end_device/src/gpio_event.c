/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include "gpio_event.h"

#include <stdio.h>
#include <string.h>

void gpio_event_init(struct gpio_event_state *st, int64_t debounce_ms)
{
	if (!st) {
		return;
	}
	st->last_state = -1;
	st->pending_state = -1;
	st->last_change_ms = 0;
	st->debounce_ms = debounce_ms;
	st->initialized = false;
}

static gpio_edge_t gpio_event_edge(int prev, int now)
{
	if (prev == 0 && now == 1) {
		return GPIO_EDGE_RISING;
	}
	if (prev == 1 && now == 0) {
		return GPIO_EDGE_FALLING;
	}
	return GPIO_EDGE_UNKNOWN;
}

gpio_edge_t gpio_event_update(struct gpio_event_state *st, int state, int64_t now_ms,
			      bool *changed)
{
	if (changed) {
		*changed = false;
	}
	if (!st || state < 0) {
		return GPIO_EDGE_NONE;
	}

	if (!st->initialized) {
		st->initialized = true;
		st->last_state = state;
		st->pending_state = state;
		st->last_change_ms = now_ms;
		return GPIO_EDGE_NONE;
	}

	if (state != st->pending_state) {
		st->pending_state = state;
		st->last_change_ms = now_ms;
		return GPIO_EDGE_NONE;
	}

	if ((now_ms - st->last_change_ms) < st->debounce_ms) {
		return GPIO_EDGE_NONE;
	}

	if (st->pending_state == st->last_state) {
		return GPIO_EDGE_NONE;
	}

	gpio_edge_t edge = gpio_event_edge(st->last_state, st->pending_state);
	st->last_state = st->pending_state;
	if (changed) {
		*changed = true;
	}
	return edge;
}

const char *gpio_edge_str(gpio_edge_t edge)
{
	switch (edge) {
	case GPIO_EDGE_RISING:
		return "rising";
	case GPIO_EDGE_FALLING:
		return "falling";
	case GPIO_EDGE_UNKNOWN:
		return "unknown";
	case GPIO_EDGE_NONE:
	default:
		return "none";
	}
}

int gpio_event_build_payload(char *buf, size_t buf_len, const char *device_id,
			     const char *device_type, const char *pin_alias, int state,
			     gpio_edge_t edge, int64_t uptime_ms, const char *run_id)
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
			       "\"event_type\":\"state_change\",\"location\":null,"
			       "\"run_id\":\"%s\",\"data\":{\"gpio\":{\"pin\":\"%s\","
			       "\"state\":%d,\"edge\":\"%s\",\"uptime_ms\":%lld}}}",
			       device_id, device_type, (long long)uptime_ms, run_id,
			       pin_alias, state, edge_str, (long long)uptime_ms);
	} else {
		len = snprintf(buf, buf_len,
			       "{\"schema_version\":\"1.0\",\"device_id\":\"%s\","
			       "\"device_type\":\"%s\",\"timestamp\":%lld,"
			       "\"event_type\":\"state_change\",\"location\":null,"
			       "\"run_id\":null,\"data\":{\"gpio\":{\"pin\":\"%s\","
			       "\"state\":%d,\"edge\":\"%s\",\"uptime_ms\":%lld}}}",
			       device_id, device_type, (long long)uptime_ms, pin_alias,
			       state, edge_str, (long long)uptime_ms);
	}

	if (len < 0 || (size_t)len >= buf_len) {
		return -1;
	}
	return len;
}
