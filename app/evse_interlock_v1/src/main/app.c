/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <state_notifier/state_notifier.h>
#include "main/app.h"
#include "sidewalk/sidewalk.h"
#include <app_ble_config.h>
#include <app_subGHz_config.h>
#include <sid_hal_memory_ifc.h>
#include <sid_hal_reset_ifc.h>
#include <stdbool.h>
#ifdef CONFIG_SIDEWALK_FILE_TRANSFER_DFU
#include "sidewalk/sbdt/dfu_file_transfer.h"
#endif

#if defined(CONFIG_BT) && defined(CONFIG_SIDEWALK_BLE)
#include <bt_app_callbacks.h>
#endif

#if defined(CONFIG_GPIO)
#include <state_notifier/notifier_gpio.h>
#endif
#if defined(CONFIG_LOG)
#include <state_notifier/notifier_log.h>
#endif
#include <sidewalk_dfu/nordic_dfu.h>
#include <buttons.h>
#include <zephyr/kernel.h>
#include <zephyr/smf.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>
#include <zephyr/random/random.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "telemetry/gpio_event.h"
#include "telemetry/evse.h"
#include "sidewalk/sidewalk_msg.h"
#include "telemetry/telemetry_evse.h"
#include "telemetry/telemetry_gpio.h"
#include "sidewalk/time_sync.h"

#include <json_printer/sidTypes2Json.h>
#include <json_printer/sidTypes2str.h>

LOG_MODULE_REGISTER(app, CONFIG_SIDEWALK_LOG_LEVEL);

#define PARAM_UNUSED (0U)
#define APP_PERIODIC_SEND_INTERVAL K_SECONDS(30)
#define APP_GPIO_DEBOUNCE_MS CONFIG_SID_END_DEVICE_GPIO_DEBOUNCE_MS
#define APP_GPIO_POLL_INTERVAL_MS CONFIG_SID_END_DEVICE_GPIO_POLL_INTERVAL_MS
#define APP_GPIO_SIM_INTERVAL_MS CONFIG_SID_END_DEVICE_GPIO_SIM_INTERVAL_MS
#define APP_DEVICE_ID CONFIG_SID_END_DEVICE_DEVICE_ID
#define APP_DEVICE_TYPE CONFIG_SID_END_DEVICE_DEVICE_TYPE
#define APP_EVSE_SAMPLE_INTERVAL_MS CONFIG_SID_END_DEVICE_EVSE_SAMPLE_INTERVAL_MS

static uint32_t persistent_link_mask;
static struct k_work_delayable periodic_send_work;
static bool periodic_send_started;
static bool app_sidewalk_ready;

#if defined(CONFIG_SID_END_DEVICE_GPIO_EVENTS) && defined(CONFIG_GPIO)
#if DT_NODE_EXISTS(DT_ALIAS(extinput0))
#define APP_GPIO_HAS_DT 1
static const struct gpio_dt_spec app_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(extinput0), gpios);
#else
#define APP_GPIO_HAS_DT 0
#endif
#define APP_GPIO_ALIAS "extinput0"

static struct gpio_callback app_gpio_cb;
static struct k_work_delayable app_gpio_debounce_work;
static struct k_timer app_gpio_poll_timer;
static struct gpio_event_state app_gpio_state;
static int app_gpio_raw_state = -1;
static bool app_gpio_use_polling;
#if defined(CONFIG_SID_END_DEVICE_GPIO_SIMULATOR)
static struct k_timer app_gpio_sim_timer;
static bool app_gpio_simulator;
static uint32_t app_gpio_sim_transitions;
#else
static const bool app_gpio_simulator = false;
#endif
#if defined(CONFIG_SID_END_DEVICE_GPIO_TEST_MODE)
static char app_gpio_run_id[16];
#else
static const char *app_gpio_run_id = NULL;
#endif

#if defined(CONFIG_SID_END_DEVICE_EVSE_ENABLED)
static struct k_work_delayable app_evse_work;
static struct k_timer app_evse_timer;
#endif

static uint32_t app_event_seq;

static int app_gpio_read_state(void)
{
#if defined(CONFIG_SID_END_DEVICE_GPIO_SIMULATOR)
	if (app_gpio_simulator) {
		return app_gpio_raw_state;
	}
#endif
#if APP_GPIO_HAS_DT
	int val = gpio_pin_get_dt(&app_gpio);
	if (val < 0) {
		LOG_ERR("GPIO read failed: %d", val);
		return val;
	}
	return val;
#else
	return app_gpio_raw_state;
#endif
}

static int64_t app_get_timestamp_ms(void)
{
	int64_t uptime_ms = k_uptime_get();
	return time_sync_get_timestamp_ms(uptime_ms);
}

static void app_next_event_id(char *buf, size_t buf_len)
{
	uint32_t seq = ++app_event_seq;
	uint32_t rand = sys_rand32_get();

	if (app_gpio_run_id && app_gpio_run_id[0] != '\0') {
		snprintf(buf, buf_len, "%s-%08x", app_gpio_run_id, seq);
	} else {
		snprintf(buf, buf_len, "%08x%08x", rand, seq);
	}
}

static void app_gpio_send_event(const char *pin_alias, int state, gpio_edge_t edge)
{
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
						  timestamp_ms, app_gpio_run_id, event_id,
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

#if defined(CONFIG_SID_END_DEVICE_EVSE_ENABLED)
static void app_evse_send_event(const struct evse_event *evt, int64_t timestamp_ms)
{
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

static void app_evse_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	struct evse_event evt = { 0 };
	int64_t ts_ms = app_get_timestamp_ms();
	if (evse_poll(&evt, ts_ms)) {
		app_evse_send_event(&evt, ts_ms);
	}
}

static void app_evse_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	(void)k_work_reschedule(&app_evse_work, K_NO_WAIT);
}
#endif

static void app_gpio_debounce_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	int state = app_gpio_read_state();
	if (state < 0) {
		return;
	}
	bool changed = false;
	gpio_edge_t edge = gpio_event_update(&app_gpio_state, state, k_uptime_get(), &changed);
	if (changed) {
		app_gpio_send_event(APP_GPIO_ALIAS, state, edge);
	}
}

static void app_gpio_schedule_debounce(int state)
{
	bool changed = false;
	(void)gpio_event_update(&app_gpio_state, state, k_uptime_get(), &changed);
	(void)k_work_reschedule(&app_gpio_debounce_work, K_MSEC(APP_GPIO_DEBOUNCE_MS));
}

static void app_gpio_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	int state = app_gpio_read_state();
	if (state >= 0) {
		app_gpio_schedule_debounce(state);
	}
}

static void app_gpio_poll_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	int state = app_gpio_read_state();
	if (state >= 0 && state != app_gpio_raw_state) {
		app_gpio_raw_state = state;
		app_gpio_schedule_debounce(state);
	}
}

#if defined(CONFIG_SID_END_DEVICE_GPIO_SIMULATOR)
static void app_gpio_sim_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);
#if defined(CONFIG_SID_END_DEVICE_GPIO_TEST_MODE)
	if (app_gpio_sim_transitions >= CONFIG_SID_END_DEVICE_GPIO_TEST_TRANSITIONS) {
		k_timer_stop(&app_gpio_sim_timer);
		LOG_INF("GPIO test: done");
		return;
	}
#endif
	app_gpio_raw_state = (app_gpio_raw_state <= 0) ? 1 : 0;
	app_gpio_sim_transitions++;
	app_gpio_schedule_debounce(app_gpio_raw_state);
}
#endif

static void app_gpio_init(void)
{
	gpio_event_init(&app_gpio_state, APP_GPIO_DEBOUNCE_MS);
#if defined(CONFIG_SID_END_DEVICE_GPIO_TEST_MODE)
	uint32_t r = sys_rand32_get();
	snprintk(app_gpio_run_id, sizeof(app_gpio_run_id), "%08x", r);
	LOG_INF("E2E run_id: %s", app_gpio_run_id);
#endif

#if defined(CONFIG_SID_END_DEVICE_GPIO_SIMULATOR)
	app_gpio_simulator = true;
#endif

#if defined(CONFIG_SID_END_DEVICE_GPIO_SIMULATOR)
	if (app_gpio_simulator) {
		LOG_INF("GPIO simulator enabled");
		app_gpio_raw_state = 0;
		k_work_init_delayable(&app_gpio_debounce_work, app_gpio_debounce_work_handler);
		k_timer_init(&app_gpio_sim_timer, app_gpio_sim_timer_handler, NULL);
		k_timer_start(&app_gpio_sim_timer, K_MSEC(APP_GPIO_SIM_INTERVAL_MS),
			      K_MSEC(APP_GPIO_SIM_INTERVAL_MS));
		return;
	}
#endif

#if APP_GPIO_HAS_DT
	if (!device_is_ready(app_gpio.port)) {
		LOG_ERR("GPIO device not ready");
		return;
	}

	int err = gpio_pin_configure_dt(&app_gpio, GPIO_INPUT);
	if (err) {
		LOG_ERR("GPIO configure failed: %d", err);
		return;
	}

	app_gpio_raw_state = app_gpio_read_state();
	k_work_init_delayable(&app_gpio_debounce_work, app_gpio_debounce_work_handler);

	err = gpio_pin_interrupt_configure_dt(&app_gpio, GPIO_INT_EDGE_BOTH);
	if (err) {
		LOG_WRN("GPIO interrupt not available (%d), using polling", err);
		app_gpio_use_polling = true;
	} else {
		gpio_init_callback(&app_gpio_cb, app_gpio_isr, BIT(app_gpio.pin));
		gpio_add_callback(app_gpio.port, &app_gpio_cb);
		LOG_INF("GPIO interrupt enabled");
	}

	if (app_gpio_use_polling) {
		k_timer_init(&app_gpio_poll_timer, app_gpio_poll_timer_handler, NULL);
		k_timer_start(&app_gpio_poll_timer, K_MSEC(APP_GPIO_POLL_INTERVAL_MS),
			      K_MSEC(APP_GPIO_POLL_INTERVAL_MS));
	}
#else
	LOG_WRN("No extinput0 alias defined; GPIO events disabled");
#endif
}
#endif /* CONFIG_SID_END_DEVICE_GPIO_EVENTS && CONFIG_GPIO */

static void on_sidewalk_event(bool in_isr, void *context)
{
	int err = sidewalk_event_send(sidewalk_event_process, NULL, NULL);
	if (err) {
		LOG_ERR("Send event err %d", err);
	};
}

static void app_handle_time_sync(const struct sid_msg *msg)
{
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
static void app_btn_send_msg(uint32_t unused)
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

static void periodic_send_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	app_btn_send_msg(PARAM_UNUSED);
	(void)k_work_schedule(&periodic_send_work, APP_PERIODIC_SEND_INTERVAL);
}

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

static int app_buttons_init(void)
{
	button_set_action_short_press(DK_BTN1, app_btn_send_msg, PARAM_UNUSED);
	button_set_action_long_press(DK_BTN1, app_btn_dfu_state, PARAM_UNUSED);
	button_set_action_short_press(DK_BTN2, app_btn_connect, PARAM_UNUSED);
	button_set_action_long_press(DK_BTN2, app_btn_factory_reset, PARAM_UNUSED);
	button_set_action(DK_BTN3, app_btn_link_switch, PARAM_UNUSED);

	return buttons_init();
}

#if defined(CONFIG_BT) && defined(CONFIG_SIDEWALK_BLE)
static bool gatt_authorize(struct bt_conn *conn, const struct bt_gatt_attr *attr)
{
	struct bt_conn_info cinfo = {};
	int ret = bt_conn_get_info(conn, &cinfo);
	if (ret != 0) {
		LOG_ERR("Failed to get id of connection err %d", ret);
		return false;
	}

	if (cinfo.id == BT_ID_SIDEWALK) {
		if (sid_ble_bt_attr_is_SMP(attr)) {
			return false;
		}
	}

#if defined(CONFIG_SIDEWALK_DFU)
	if (cinfo.id == BT_ID_SMP_DFU) {
		if (sid_ble_bt_attr_is_SIDEWALK(attr)) {
			return false;
		}
	}
#endif //defined(CONFIG_SIDEWALK_DFU)
	return true;
}

static const struct bt_gatt_authorization_cb gatt_authorization_callbacks = {
	.read_authorize = gatt_authorize,
	.write_authorize = gatt_authorize,
};
#endif

#define MAX_TIME_SYNC_INTERVALS 10
static uint16_t default_sync_intervals_h[MAX_TIME_SYNC_INTERVALS] = { 2, 4, 8,
								      12 }; // default GCS intervals
static struct sid_time_sync_config default_time_sync_config = {
	.adaptive_sync_intervals_h = default_sync_intervals_h,
	.num_intervals = sizeof(default_sync_intervals_h) / sizeof(default_sync_intervals_h[0]),
};

void app_start(void)
{
	time_sync_init();
	if (app_buttons_init()) {
		LOG_ERR("Cannot init buttons");
	}
	k_work_init_delayable(&periodic_send_work, periodic_send_work_handler);
#if defined(CONFIG_SID_END_DEVICE_GPIO_EVENTS) && defined(CONFIG_GPIO)
	app_gpio_init();
#endif

#if defined(CONFIG_SID_END_DEVICE_EVSE_ENABLED)
	if (evse_init()) {
		LOG_ERR("EVSE init failed");
	} else {
		k_work_init_delayable(&app_evse_work, app_evse_work_handler);
		k_timer_init(&app_evse_timer, app_evse_timer_handler, NULL);
		k_timer_start(&app_evse_timer, K_MSEC(APP_EVSE_SAMPLE_INTERVAL_MS),
			      K_MSEC(APP_EVSE_SAMPLE_INTERVAL_MS));
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
	int err = bt_gatt_authorization_cb_register(&gatt_authorization_callbacks);
	if (err) {
		LOG_ERR("Registering GATT authorization callbacks failed (err %d)", err);
		return;
	}
#endif
	sidewalk_start(&sid_ctx);
	sidewalk_event_send(sidewalk_event_platform_init, NULL, NULL);
	sidewalk_event_send(sidewalk_event_autostart, NULL, NULL);
}
