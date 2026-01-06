/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#ifndef GPIO_EVENT_H
#define GPIO_EVENT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
	GPIO_EDGE_NONE = 0,
	GPIO_EDGE_RISING,
	GPIO_EDGE_FALLING,
	GPIO_EDGE_UNKNOWN,
} gpio_edge_t;

struct gpio_event_state {
	int last_state;
	int pending_state;
	int64_t last_change_ms;
	int64_t debounce_ms;
	bool initialized;
};

void gpio_event_init(struct gpio_event_state *st, int64_t debounce_ms);

gpio_edge_t gpio_event_update(struct gpio_event_state *st, int state, int64_t now_ms,
			      bool *changed);

const char *gpio_edge_str(gpio_edge_t edge);

#endif /* GPIO_EVENT_H */
