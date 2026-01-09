/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef APP_GPIO_H
#define APP_GPIO_H

#include "telemetry/gpio_event.h"

typedef void (*app_gpio_event_handler_t)(const char *pin_alias, int state, gpio_edge_t edge);

void app_gpio_init(app_gpio_event_handler_t handler);

#endif /* APP_GPIO_H */
