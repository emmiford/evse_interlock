/*
 * [LINE-CURRENT] Periodic sampling loop for upstream current clamp.
 */
#include "main/app_line_current.h"

#include "telemetry/line_current.h"
#include "sidewalk/time_sync.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app);

#define APP_LINE_CURRENT_SAMPLE_INTERVAL_MS CONFIG_SID_END_DEVICE_LINE_CURRENT_SAMPLE_INTERVAL_MS

static struct k_work_delayable app_line_current_work;
static struct k_timer app_line_current_timer;
static app_line_current_event_handler_t app_line_current_event_handler;

static int64_t app_line_current_get_timestamp_ms(void)
{
	int64_t uptime_ms = k_uptime_get();
	return time_sync_get_timestamp_ms(uptime_ms);
}

static void app_line_current_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	if (!app_line_current_event_handler) {
		return;
	}

	struct line_current_event evt = { 0 };
	int64_t ts_ms = app_line_current_get_timestamp_ms();
	if (line_current_poll(&evt)) {
		app_line_current_event_handler(&evt, ts_ms);
	}
}

static void app_line_current_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	(void)k_work_reschedule(&app_line_current_work, K_NO_WAIT);
}

int app_line_current_init(app_line_current_event_handler_t handler)
{
	app_line_current_event_handler = handler;
	int err = line_current_init();
	if (err) {
		return err;
	}

	k_work_init_delayable(&app_line_current_work, app_line_current_work_handler);
	k_timer_init(&app_line_current_timer, app_line_current_timer_handler, NULL);
	k_timer_start(&app_line_current_timer, K_MSEC(APP_LINE_CURRENT_SAMPLE_INTERVAL_MS),
		      K_MSEC(APP_LINE_CURRENT_SAMPLE_INTERVAL_MS));
	return 0;
}
