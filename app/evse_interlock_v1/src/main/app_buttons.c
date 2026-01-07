/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "main/app_buttons.h"
#include "sidewalk/sidewalk.h"
#include "sidewalk/sidewalk_msg.h"

#include <buttons.h>
#include <sidewalk_dfu/nordic_dfu.h>
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#if defined(CONFIG_STATE_NOTIFIER)
#include <state_notifier/state_notifier.h>
#endif

LOG_MODULE_DECLARE(app);

#define APP_BUTTON_PARAM_UNUSED 0U

static void app_event_exit_dfu_mode(sidewalk_ctx_t *sid, void *ctx)
{
	int err = -ENOTSUP;
	// Exit from DFU state
#if defined(CONFIG_SIDEWALK_DFU_SERVICE_BLE)
	err = nordic_dfu_ble_stop();
#endif
	if (err) {
		LOG_ERR("dfu stop err %d", err);
	}
}

static void app_event_enter_dfu_mode(sidewalk_ctx_t *sid, void *ctx)
{
	int err = -ENOTSUP;

	LOG_INF("Entering into DFU mode");
#if defined(CONFIG_SIDEWALK_DFU_SERVICE_BLE)
	err = nordic_dfu_ble_start();
#endif
	if (err) {
		LOG_ERR("dfu start err %d", err);
	}
}

static void app_btn_dfu_state(uint32_t unused)
{
	ARG_UNUSED(unused);
	static bool go_to_dfu_state = true;
	if (go_to_dfu_state) {
		sidewalk_event_send(app_event_enter_dfu_mode, NULL, NULL);
	} else {
		sidewalk_event_send(app_event_exit_dfu_mode, NULL, NULL);
	}

	go_to_dfu_state = !go_to_dfu_state;
}

static void app_btn_connect(uint32_t unused)
{
	ARG_UNUSED(unused);
	(void)sidewalk_event_send(sidewalk_event_connect, NULL, NULL);
}

static void app_btn_factory_reset(uint32_t unused)
{
	ARG_UNUSED(unused);
	(void)sidewalk_event_send(sidewalk_event_factory_reset, NULL, NULL);
}

static void app_btn_link_switch(uint32_t unused)
{
	ARG_UNUSED(unused);
	(void)sidewalk_event_send(sidewalk_event_link_switch, NULL, NULL);
}

void app_btn_send_msg(uint32_t unused)
{
	ARG_UNUSED(unused);

	LOG_INF("Send hello message");
	const char payload[] = "hello";
	int err = sidewalk_send_notify_json(payload, sizeof(payload));
	if (err) {
		LOG_ERR("Send event err %d", err);
	} else {
#if defined(CONFIG_STATE_NOTIFIER)
		application_state_sending(&global_state_notifier, true);
#endif
	}
}

int app_buttons_init(void)
{
	button_set_action_short_press(DK_BTN1, app_btn_send_msg, APP_BUTTON_PARAM_UNUSED);
	button_set_action_long_press(DK_BTN1, app_btn_dfu_state, APP_BUTTON_PARAM_UNUSED);
	button_set_action_short_press(DK_BTN2, app_btn_connect, APP_BUTTON_PARAM_UNUSED);
	button_set_action_long_press(DK_BTN2, app_btn_factory_reset, APP_BUTTON_PARAM_UNUSED);
	button_set_action(DK_BTN3, app_btn_link_switch, APP_BUTTON_PARAM_UNUSED);

	return buttons_init();
}
