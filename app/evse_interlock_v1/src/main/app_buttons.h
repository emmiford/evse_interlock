/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef APP_BUTTONS_H
#define APP_BUTTONS_H

#include <stdint.h>

int app_buttons_init(void);
void app_btn_send_msg(uint32_t unused);

#endif /* APP_BUTTONS_H */
