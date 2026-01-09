/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "main/app_gpio.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

LOG_MODULE_DECLARE(app);

#if defined(CONFIG_SID_END_DEVICE_GPIO_EVENTS) && defined(CONFIG_GPIO)
#define APP_GPIO_DEBOUNCE_MS CONFIG_SID_END_DEVICE_GPIO_DEBOUNCE_MS
#define APP_GPIO_POLL_INTERVAL_MS CONFIG_SID_END_DEVICE_GPIO_POLL_INTERVAL_MS
#define APP_GPIO_SIM_INTERVAL_MS 2000

/* [BOILERPLATE] GPIO ingest plumbing: ISR/poll + debounce + edge reporting. */
#if DT_NODE_EXISTS(DT_ALIAS(hvac))
#define APP_GPIO_HAS_DT 1
static const struct gpio_dt_spec app_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(hvac), gpios);
#else
#define APP_GPIO_HAS_DT 0
#endif
#define APP_GPIO_ALIAS "hvac"

static struct gpio_callback app_gpio_cb;
static struct k_work_delayable app_gpio_debounce_work;
static struct k_timer app_gpio_poll_timer;
static struct gpio_event_state app_gpio_state;
static int app_gpio_raw_state = -1;
static bool app_gpio_use_polling;
static app_gpio_event_handler_t app_gpio_event_handler;
#if defined(CONFIG_SID_END_DEVICE_GPIO_SIMULATOR)
static struct k_timer app_gpio_sim_timer;
static bool app_gpio_simulator;
#else
static const bool app_gpio_simulator = false;
#endif

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

static void app_gpio_emit_event(int state, gpio_edge_t edge)
{
	if (app_gpio_event_handler) {
		app_gpio_event_handler(APP_GPIO_ALIAS, state, edge);
	}
}

static void app_gpio_debounce_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	/* [BOILERPLATE] Work item to finish debounce and emit a single edge. */
	int state = app_gpio_read_state();
	if (state < 0) {
		return;
	}
	bool changed = false;
	gpio_edge_t edge = gpio_event_update(&app_gpio_state, state, k_uptime_get(), &changed);
	if (changed) {
		app_gpio_emit_event(state, edge);
	}
}

static void app_gpio_schedule_debounce(int state)
{
	/* [BOILERPLATE] Capture pending state and defer to debounce timer. */
	bool changed = false;
	(void)gpio_event_update(&app_gpio_state, state, k_uptime_get(), &changed);
	(void)k_work_reschedule(&app_gpio_debounce_work, K_MSEC(APP_GPIO_DEBOUNCE_MS));
}

static void app_gpio_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	/* [BOILERPLATE] GPIO ISR glue: read input and schedule debounce. */
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
	/* [BOILERPLATE] Polling fallback for platforms without GPIO interrupts. */
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
	app_gpio_raw_state = (app_gpio_raw_state <= 0) ? 1 : 0;
	app_gpio_schedule_debounce(app_gpio_raw_state);
}
#endif

void app_gpio_init(app_gpio_event_handler_t handler)
{
	app_gpio_event_handler = handler;
	/* [BOILERPLATE] Configure GPIO input and debounce path. */
	gpio_event_init(&app_gpio_state, APP_GPIO_DEBOUNCE_MS);
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
	LOG_WRN("No hvac alias defined; GPIO events disabled");
#endif
}
#else
void app_gpio_init(app_gpio_event_handler_t handler)
{
	ARG_UNUSED(handler);
}
#endif /* CONFIG_SID_END_DEVICE_GPIO_EVENTS && CONFIG_GPIO */
