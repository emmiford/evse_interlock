/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
/*
 * [3P-GLUE] Sidewalk + Zephyr integration glue and callbacks.
 * [BOILERPLATE] Typical app wiring (threads, timers, ISR, work queues).
 * [TELEMETRY] Builds and sends GPIO/EVSE payloads.
 */

#include <state_notifier/state_notifier.h>
#include "main/app.h"
#include "main/app_ble_auth.h"
#include "main/app_buttons.h"
#include "main/app_evse.h"
#include "main/app_line_current.h"
#include "main/app_gpio.h"
#include "main/rtt_heartbeat.h"
#include "sidewalk/sidewalk.h"
#include <app_ble_config.h>
#include <app_subGHz_config.h>
#include <sid_hal_memory_ifc.h>
#include <sid_hal_reset_ifc.h>
#include <stdbool.h>
#ifdef CONFIG_SIDEWALK_FILE_TRANSFER_DFU
#include "sidewalk/sbdt/dfu_file_transfer.h"
#endif

#if defined(CONFIG_GPIO)
#include <state_notifier/notifier_gpio.h>
#endif
#if defined(CONFIG_LOG)
#include <state_notifier/notifier_log.h>
#endif
#include <zephyr/kernel.h>
#include <zephyr/smf.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/random/random.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "telemetry/evse.h"
#include "sidewalk/sidewalk_msg.h"
#include "telemetry/telemetry_evse.h"
#include "telemetry/telemetry_gpio.h"
#include "telemetry/telemetry_line_current.h"
#include "sidewalk/time_sync.h"

#include <json_printer/sidTypes2Json.h>
#include <json_printer/sidTypes2str.h>

LOG_MODULE_REGISTER(app, CONFIG_SIDEWALK_LOG_LEVEL);

#define APP_PERIODIC_SEND_INTERVAL K_SECONDS(30)
#define APP_DEVICE_ID CONFIG_SID_END_DEVICE_DEVICE_ID
#define APP_DEVICE_TYPE CONFIG_SID_END_DEVICE_DEVICE_TYPE

static uint32_t persistent_link_mask;
static struct k_work_delayable periodic_send_work;
static bool periodic_send_started;
static bool app_sidewalk_ready;

static uint32_t app_event_seq;

static int64_t app_get_timestamp_ms(void)
{
	int64_t uptime_ms = k_uptime_get();
	return time_sync_get_timestamp_ms(uptime_ms);
}

/* [BOILERPLATE] Event ID generator for telemetry correlation. */
static void app_next_event_id(char *buf, size_t buf_len)
{
	uint32_t seq = ++app_event_seq;
	uint32_t rand = sys_rand32_get();
	snprintf(buf, buf_len, "%08x%08x", rand, seq);
}

#if defined(CONFIG_SID_END_DEVICE_GPIO_EVENTS) && defined(CONFIG_GPIO)
static void app_gpio_send_event(const char *pin_alias, int state, gpio_edge_t edge)
{
	/* [TELEMETRY] GPIO event payload construction + Sidewalk uplink. */
	if (!app_sidewalk_ready) {
		LOG_WRN("Sidewalk not ready; drop gpio event");
		return;
	}

	int64_t timestamp_ms = app_get_timestamp_ms();
	bool time_anomaly = time_sync_time_anomaly();
	char event_id[32];
	char payload[384];
	app_next_event_id(event_id, sizeof(event_id));
	int len = telemetry_build_gpio_payload_ex(payload, sizeof(payload), APP_DEVICE_ID,
						  APP_DEVICE_TYPE, pin_alias, state, edge,
						  timestamp_ms, NULL, event_id,
						  time_anomaly);
	if (len < 0) {
		LOG_ERR("GPIO payload format failed");
		return;
	}

	LOG_INF("GPIO event: %s state=%d edge=%s timestamp_ms=%" PRId64, pin_alias, state,
		gpio_edge_str(edge), timestamp_ms);

	int err = sidewalk_send_notify_json(payload, (size_t)len);
	if (err) {
		LOG_ERR("Sidewalk send: err %d", err);
	} else {
#if defined(CONFIG_STATE_NOTIFIER)
		application_state_sending(&global_state_notifier, true);
#endif
		LOG_INF("Sidewalk send: ok 0");
	}
}
#endif

#if defined(CONFIG_SID_END_DEVICE_EVSE_ENABLED)
static void app_evse_send_event(const struct evse_event *evt, int64_t timestamp_ms)
{
	/* [TELEMETRY] EVSE event payload construction + Sidewalk uplink. */
	if (!app_sidewalk_ready) {
		LOG_WRN("Sidewalk not ready; drop evse event");
		return;
	}

	char event_id[32];
	char payload[384];
	app_next_event_id(event_id, sizeof(event_id));
	int len = telemetry_build_evse_payload_ex(payload, sizeof(payload), APP_DEVICE_ID,
						  APP_DEVICE_TYPE, timestamp_ms, evt, event_id,
						  time_sync_time_anomaly());
	if (len < 0) {
		LOG_ERR("EVSE payload format failed");
		return;
	}

	LOG_INF("EVSE event: pilot=%c prox=%d duty=%.2f current=%.2fA energy=%.4f",
		evse_pilot_state_to_char(evt->pilot_state), evt->proximity_detected,
		(double)evt->pwm_duty_cycle, (double)evt->current_draw_a,
		(double)evt->energy_kwh);

	int err = sidewalk_send_notify_json(payload, (size_t)len);
	if (err) {
		LOG_ERR("Sidewalk send: err %d", err);
	}
}
#endif

#if defined(CONFIG_SID_END_DEVICE_LINE_CURRENT_ENABLED)
static void app_line_current_send_event(const struct line_current_event *evt, int64_t timestamp_ms)
{
	/* [TELEMETRY] Line current payload construction + Sidewalk uplink. */
	if (!app_sidewalk_ready) {
		LOG_WRN("Sidewalk not ready; drop line current event");
		return;
	}

	char event_id[32];
	char payload[256];
	app_next_event_id(event_id, sizeof(event_id));
	int len = telemetry_build_line_current_payload_ex(payload, sizeof(payload), APP_DEVICE_ID,
							  APP_DEVICE_TYPE, timestamp_ms, evt,
							  event_id, time_sync_time_anomaly());
	if (len < 0) {
		LOG_ERR("Line current payload format failed");
		return;
	}

	LOG_INF("Line current event: current=%.2fA", (double)evt->current_a);

	int err = sidewalk_send_notify_json(payload, (size_t)len);
	if (err) {
		LOG_ERR("Sidewalk send: err %d", err);
	}
}
#endif

static void on_sidewalk_event(bool in_isr, void *context)
{
	/* [3P-GLUE] Sidewalk SDK callback entrypoint (signature required). */
	int err = sidewalk_event_send(sidewalk_event_process, NULL, NULL);
	if (err) {
		LOG_ERR("Send event err %d", err);
	};
}

static void app_handle_time_sync(const struct sid_msg *msg)
{
	/* [TELEMETRY] Parse time_sync downlink to switch timestamp source. */
	if (!msg || !msg->data || msg->size == 0) {
		return;
	}

	char buf[128];
	size_t copy_len = MIN(msg->size, sizeof(buf) - 1);
	memcpy(buf, msg->data, copy_len);
	buf[copy_len] = '\0';

	if (!strstr(buf, "\"cmd\":\"time_sync\"")) {
		return;
	}

	char *epoch_ptr = strstr(buf, "\"epoch_ms\":");
	if (!epoch_ptr) {
		return;
	}

	int64_t epoch_ms = strtoll(epoch_ptr + strlen("\"epoch_ms\":"), NULL, 10);
	int64_t now_ms = k_uptime_get();
	time_sync_apply_epoch_ms(epoch_ms, now_ms);
	LOG_INF("Time sync applied: epoch_ms=%" PRId64, epoch_ms);
}

static void on_sidewalk_msg_received(const struct sid_msg_desc *msg_desc, const struct sid_msg *msg,
				     void *context)
{
	/* [3P-GLUE] Sidewalk receive callback; bridge to app/time_sync logic. */
	LOG_HEXDUMP_INF((uint8_t *)msg->data, msg->size, "Message received success");
	printk(JSON_NEW_LINE(JSON_OBJ(JSON_NAME(
		"on_msg_received", JSON_OBJ(JSON_VAL_sid_msg_desc("sid_msg_desc", msg_desc, 1))))));
#if defined(CONFIG_STATE_NOTIFIER)
	application_state_receiving(&global_state_notifier, true);
	application_state_receiving(&global_state_notifier, false);
#endif
	app_handle_time_sync(msg);

#ifdef CONFIG_SID_END_DEVICE_ECHO_MSGS
	if (msg_desc->type == SID_MSG_TYPE_GET || msg_desc->type == SID_MSG_TYPE_SET) {
		LOG_INF("Send echo message");
		struct sid_msg_desc desc = {
			.type = (msg_desc->type == SID_MSG_TYPE_GET) ? SID_MSG_TYPE_RESPONSE :
							     SID_MSG_TYPE_NOTIFY,
			.id = (msg_desc->type == SID_MSG_TYPE_GET) ? msg_desc->id : msg_desc->id + 1,
			.link_type = SID_LINK_TYPE_ANY,
			.link_mode = SID_LINK_MODE_CLOUD,
		};

		int err = sidewalk_send_msg_copy(&desc, msg->data, msg->size);
		if (err) {
			LOG_ERR("Send event err %d", err);
		} else {
#if defined(CONFIG_STATE_NOTIFIER)
			application_state_sending(&global_state_notifier, true);
#endif
		}
	};
#endif /* CONFIG_SID_END_DEVICE_ECHO_MSGS */
}

static void on_sidewalk_msg_sent(const struct sid_msg_desc *msg_desc, void *context)
{
	/* [3P-GLUE] Sidewalk send callback. */
	LOG_INF("Message send success");
	printk(JSON_NEW_LINE(JSON_OBJ(JSON_NAME(
		"on_msg_sent", JSON_OBJ(JSON_VAL_sid_msg_desc("sid_msg_desc", msg_desc, 0))))));
#if defined(CONFIG_STATE_NOTIFIER)
	application_state_sending(&global_state_notifier, false);
#endif
}

static void on_sidewalk_send_error(sid_error_t error, const struct sid_msg_desc *msg_desc,
				   void *context)
{
	/* [3P-GLUE] Sidewalk error callback. */
	LOG_ERR("Message send err %d (%s)", (int)error, SID_ERROR_T_STR(error));
	printk(JSON_NEW_LINE(JSON_OBJ(JSON_NAME(
		"on_send_error",
		JSON_OBJ(JSON_LIST_2(JSON_VAL_sid_error_t("error", error),
				     JSON_VAL_sid_msg_desc("sid_msg_desc", msg_desc, 0)))))));
#if defined(CONFIG_STATE_NOTIFIER)
	application_state_sending(&global_state_notifier, false);
#endif
}

static void on_sidewalk_factory_reset(void *context)
{
	/* [3P-GLUE] Sidewalk factory reset callback. */
	ARG_UNUSED(context);
#ifndef CONFIG_SID_END_DEVICE_CLI
	LOG_INF("Factory reset notification received from sid api");
	if (sid_hal_reset(SID_HAL_RESET_NORMAL)) {
		LOG_WRN("Cannot reboot");
	}
#else
	LOG_INF("sid_set_factory_reset success");
#endif
}

static void on_sidewalk_status_changed(const struct sid_status *status, void *context)
{
	/* [3P-GLUE] Sidewalk status callback; start/stop periodic telemetry. */
	int err = 0;
	uint32_t new_link_mask = status->detail.link_status_mask;
	struct sid_status *new_status = sid_hal_malloc(sizeof(struct sid_status));
	if (!new_status) {
		LOG_ERR("Failed to allocate memory for new status value");
	} else {
		memcpy(new_status, status, sizeof(struct sid_status));
	}
	err = sidewalk_event_send(sidewalk_event_new_status, new_status, sid_hal_free);

#if defined(CONFIG_STATE_NOTIFIER)
	switch (status->state) {
	case SID_STATE_READY:
	case SID_STATE_SECURE_CHANNEL_READY:
		app_sidewalk_ready = true;
		application_state_connected(&global_state_notifier, true);
		LOG_INF("Status changed: ready");
		if (!periodic_send_started) {
			periodic_send_started = true;
			(void)k_work_schedule(&periodic_send_work, K_SECONDS(5));
		}
		break;
	case SID_STATE_NOT_READY:
		app_sidewalk_ready = false;
		application_state_connected(&global_state_notifier, false);
		LOG_INF("Status changed: not ready");
		if (periodic_send_started) {
			periodic_send_started = false;
			(void)k_work_cancel_delayable(&periodic_send_work);
		}
		break;
	case SID_STATE_ERROR:
		app_sidewalk_ready = false;
		application_state_error(&global_state_notifier, true);
		LOG_INF("Status not changed: error");
		if (periodic_send_started) {
			periodic_send_started = false;
			(void)k_work_cancel_delayable(&periodic_send_work);
		}
		break;
	}

	if (err) {
		LOG_ERR("Send event err %d", err);
	}

	application_state_registered(&global_state_notifier,
				     status->detail.registration_status == SID_STATUS_REGISTERED);
	application_state_time_sync(&global_state_notifier,
				    status->detail.time_sync_status == SID_STATUS_TIME_SYNCED);
#endif /* CONFIG_STATE_NOTIFIER */

	LOG_INF("Device %sregistered, Time Sync %s, Link status: {BLE: %s, FSK: %s, LoRa: %s}",
		(SID_STATUS_REGISTERED == status->detail.registration_status) ? "Is " : "Un",
		(SID_STATUS_TIME_SYNCED == status->detail.time_sync_status) ? "Success" : "Fail",
		(new_link_mask & SID_LINK_TYPE_1) ? "Up" : "Down",
		(new_link_mask & SID_LINK_TYPE_2) ? "Up" : "Down",
		(new_link_mask & SID_LINK_TYPE_3) ? "Up" : "Down");

	for (int i = 0; i < SID_LINK_TYPE_MAX_IDX; i++) {
		enum sid_link_mode mode =
			(enum sid_link_mode)status->detail.supported_link_modes[i];

		if (mode) {
			LOG_INF("Link mode on %s = {Cloud: %s, Mobile: %s}",
				(SID_LINK_TYPE_1_IDX == i) ? "BLE" :
				(SID_LINK_TYPE_2_IDX == i) ? "FSK" :
				(SID_LINK_TYPE_3_IDX == i) ? "LoRa" :
							     "unknow",
				(mode & SID_LINK_MODE_CLOUD) ? "True" : "False",
				(mode & SID_LINK_MODE_MOBILE) ? "True" : "False");
		}
	}
}
static void periodic_send_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	app_btn_send_msg(0U);
	(void)k_work_schedule(&periodic_send_work, APP_PERIODIC_SEND_INTERVAL);
}

#define MAX_TIME_SYNC_INTERVALS 10
static uint16_t default_sync_intervals_h[MAX_TIME_SYNC_INTERVALS] = { 2, 4, 8,
							      12 }; // default GCS intervals
static struct sid_time_sync_config default_time_sync_config = {
	.adaptive_sync_intervals_h = default_sync_intervals_h,
	.num_intervals = sizeof(default_sync_intervals_h) / sizeof(default_sync_intervals_h[0]),
};

void app_start(void)
{
	/* [BOILERPLATE] App init and periodic work scheduling. */
	time_sync_init();
	if (app_buttons_init()) {
		LOG_ERR("Cannot init buttons");
	}
	k_work_init_delayable(&periodic_send_work, periodic_send_work_handler);
#if defined(CONFIG_SID_END_DEVICE_GPIO_EVENTS) && defined(CONFIG_GPIO)
	app_gpio_init(app_gpio_send_event);
#endif

#if defined(CONFIG_SID_END_DEVICE_EVSE_ENABLED)
	if (app_evse_init(app_evse_send_event)) {
		LOG_ERR("EVSE init failed");
	}
#endif

#if defined(CONFIG_SID_END_DEVICE_LINE_CURRENT_ENABLED)
	if (app_line_current_init(app_line_current_send_event)) {
		LOG_ERR("Line current init failed");
	}
#endif

#if defined(CONFIG_STATE_NOTIFIER)
#if defined(CONFIG_GPIO)
	state_watch_init_gpio(&global_state_notifier);
#endif
#if defined(CONFIG_LOG)
	state_watch_init_log(&global_state_notifier);
#endif
	application_state_working(&global_state_notifier, true);
#endif

#if defined(CONFIG_SID_END_DEVICE_RTT_HEARTBEAT)
	rtt_heartbeat_start();
#endif

	/* THIRD-PARTY BOUNDARY - DO NOT MODIFY: Sidewalk SDK callback wiring below. */
	static sidewalk_ctx_t sid_ctx = { 0 };

	static struct sid_event_callbacks event_callbacks = {
		.context = &sid_ctx,
		.on_event = on_sidewalk_event,
		.on_msg_received = on_sidewalk_msg_received,
		.on_msg_sent = on_sidewalk_msg_sent,
		.on_send_error = on_sidewalk_send_error,
		.on_status_changed = on_sidewalk_status_changed,
		.on_factory_reset = on_sidewalk_factory_reset,
	};

	struct sid_end_device_characteristics dev_ch = {
		.type = SID_END_DEVICE_TYPE_STATIC,
		.power_type = SID_END_DEVICE_POWERED_BY_BATTERY_AND_LINE_POWER,
		.qualification_id = 0x0001,
	};

	sid_ctx.config = (struct sid_config){
		.link_mask = persistent_link_mask,
		.dev_ch = dev_ch,
		.callbacks = &event_callbacks,
#if defined(CONFIG_SIDEWALK_BLE)
		.link_config = app_get_ble_config(),
#else
		.link_config = NULL,
#endif
		.sub_ghz_link_config = app_get_sub_ghz_config(),
		.log_config = NULL,
		.time_sync_config = &default_time_sync_config,
	};

#if defined(CONFIG_BT) && defined(CONFIG_SIDEWALK_BLE)
	int err = app_ble_auth_init();
	if (err) {
		LOG_ERR("Registering GATT authorization callbacks failed (err %d)", err);
		return;
	}
#endif
	sidewalk_start(&sid_ctx);
	sidewalk_event_send(sidewalk_event_platform_init, NULL, NULL);
	sidewalk_event_send(sidewalk_event_autostart, NULL, NULL);
}
