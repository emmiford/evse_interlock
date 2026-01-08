/*
 * [LINE-CURRENT] App wrapper to poll line current and emit events.
 */
#ifndef APP_LINE_CURRENT_H
#define APP_LINE_CURRENT_H

#include <stdint.h>

struct line_current_event;

typedef void (*app_line_current_event_handler_t)(const struct line_current_event *evt,
						 int64_t timestamp_ms);

int app_line_current_init(app_line_current_event_handler_t handler);

#endif /* APP_LINE_CURRENT_H */
