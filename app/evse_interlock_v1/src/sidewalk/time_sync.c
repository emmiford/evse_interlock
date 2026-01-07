/*
 * [TELEMETRY] Timestamp helper: uptime until epoch sync, clamp backward time.
 * [BOILERPLATE] Minimal state holder used across app + tests.
 */
#include "sidewalk/time_sync.h"

/* BEGIN PROJECT CODE: timestamp semantics used by telemetry payloads. */

static int64_t epoch_at_boot_ms;
static bool time_synced;
static int64_t last_timestamp_ms;
static bool time_anomaly;

/* [BOILERPLATE] Reset timestamp source and anomaly tracking. */
void time_sync_init(void)
{
	epoch_at_boot_ms = 0;
	time_synced = false;
	last_timestamp_ms = -1;
	time_anomaly = false;
}

/* [TELEMETRY] Apply downlink epoch; record anomaly if it would move time backward. */
void time_sync_apply_epoch_ms(int64_t epoch_ms, int64_t uptime_ms)
{
	if (last_timestamp_ms >= 0 && epoch_ms < last_timestamp_ms) {
		time_anomaly = true;
		epoch_at_boot_ms = last_timestamp_ms - uptime_ms;
	} else {
		epoch_at_boot_ms = epoch_ms - uptime_ms;
	}
	time_synced = true;
}

/* [TELEMETRY] Return monotonic timestamp; clamp backward jumps and flag time_anomaly. */
int64_t time_sync_get_timestamp_ms(int64_t uptime_ms)
{
	int64_t ts_ms = time_synced ? (epoch_at_boot_ms + uptime_ms) : uptime_ms;

	if (last_timestamp_ms >= 0 && ts_ms < last_timestamp_ms) {
		time_anomaly = true;
		if (time_synced) {
			epoch_at_boot_ms = last_timestamp_ms - uptime_ms;
		}
		ts_ms = last_timestamp_ms;
	}

	last_timestamp_ms = ts_ms;
	return ts_ms;
}

bool time_sync_is_synced(void)
{
	return time_synced;
}

bool time_sync_time_anomaly(void)
{
	return time_anomaly;
}
