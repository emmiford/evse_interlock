/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef APP_BLE_AUTH_H
#define APP_BLE_AUTH_H

#if defined(CONFIG_BT) && defined(CONFIG_SIDEWALK_BLE)
int app_ble_auth_init(void);
#endif

#endif /* APP_BLE_AUTH_H */
