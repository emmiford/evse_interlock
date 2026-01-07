/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "main/app_evse.h"

#include "telemetry/evse.h"
#include "sidewalk/time_sync.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app);

#define APP_EVSE_SAMPLE_INTERVAL_MS CONFIG_SID_END_DEVICE_EVSE_SAMPLE_INTERVAL_MS

static struct k_work_delayable app_evse_work;
static struct k_timer app_evse_timer;
static app_evse_event_handler_t app_evse_event_handler;

static int64_t app_evse_get_timestamp_ms(void)
{
	int64_t uptime_ms = k_uptime_get();
	return time_sync_get_timestamp_ms(uptime_ms);
}

static void app_evse_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	if (!app_evse_event_handler) {
		return;
	}

	struct evse_event evt = { 0 };
	int64_t ts_ms = app_evse_get_timestamp_ms();
	if (evse_poll(&evt, ts_ms)) {
		app_evse_event_handler(&evt, ts_ms);
	}
}

static void app_evse_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	(void)k_work_reschedule(&app_evse_work, K_NO_WAIT);
}

int app_evse_init(app_evse_event_handler_t handler)
{
	app_evse_event_handler = handler;
	int err = evse_init();
	if (err) {
		return err;
	}

	k_work_init_delayable(&app_evse_work, app_evse_work_handler);
	k_timer_init(&app_evse_timer, app_evse_timer_handler, NULL);
	k_timer_start(&app_evse_timer, K_MSEC(APP_EVSE_SAMPLE_INTERVAL_MS),
		      K_MSEC(APP_EVSE_SAMPLE_INTERVAL_MS));
	return 0;
}
