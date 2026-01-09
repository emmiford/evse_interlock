/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "main/rtt_heartbeat.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(rtt_heartbeat, CONFIG_SIDEWALK_LOG_LEVEL);

#define RTT_HEARTBEAT_INTERVAL K_SECONDS(5)

static struct k_work_delayable rtt_heartbeat_work;

static void rtt_heartbeat_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	LOG_INF("RTT heartbeat");
	(void)k_work_schedule(&rtt_heartbeat_work, RTT_HEARTBEAT_INTERVAL);
}

void rtt_heartbeat_start(void)
{
	k_work_init_delayable(&rtt_heartbeat_work, rtt_heartbeat_work_handler);
	(void)k_work_schedule(&rtt_heartbeat_work, RTT_HEARTBEAT_INTERVAL);
}
