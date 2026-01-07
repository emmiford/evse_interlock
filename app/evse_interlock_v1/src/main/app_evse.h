/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef APP_EVSE_H
#define APP_EVSE_H

#include <stdint.h>

struct evse_event;

typedef void (*app_evse_event_handler_t)(const struct evse_event *evt, int64_t timestamp_ms);

int app_evse_init(app_evse_event_handler_t handler);

#endif /* APP_EVSE_H */
