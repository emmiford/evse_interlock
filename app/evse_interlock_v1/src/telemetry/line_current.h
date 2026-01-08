/*
 * [LINE-CURRENT] External line current monitoring via ADC clamp.
 * [BOILERPLATE] Sampling interface and event type.
 */
#ifndef LINE_CURRENT_H
#define LINE_CURRENT_H

#include <stdbool.h>

struct line_current_event {
	bool send;
	float current_a;
	const char *event_type;
};

int line_current_init(void);
bool line_current_poll(struct line_current_event *evt);

#endif /* LINE_CURRENT_H */
