/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
/*
 * [BOILERPLATE] Generic debounce/edge detector (no EVSE-specific semantics).
 * This is reusable glue that feeds EVSE safety gating and telemetry.
 */
#include "telemetry/gpio_event.h"

/* BEGIN PROJECT CODE: debounce + edge tracking logic. */

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

/* [BOILERPLATE] Typical debounce flow: pending state + elapsed time. */
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
