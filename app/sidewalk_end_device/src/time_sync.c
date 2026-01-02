/*
 * Simple epoch time sync helper
 */
#include "time_sync.h"

static int64_t epoch_at_boot_ms;
static bool time_synced;

void time_sync_init(void)
{
	epoch_at_boot_ms = 0;
	time_synced = false;
}

void time_sync_apply_epoch_ms(int64_t epoch_ms, int64_t uptime_ms)
{
	epoch_at_boot_ms = epoch_ms - uptime_ms;
	time_synced = true;
}

int64_t time_sync_get_timestamp_ms(int64_t uptime_ms)
{
	return time_synced ? (epoch_at_boot_ms + uptime_ms) : uptime_ms;
}

bool time_sync_is_synced(void)
{
	return time_synced;
}
