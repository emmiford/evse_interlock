/*
 * Simple epoch time sync helper
 */
#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <stdbool.h>
#include <stdint.h>

void time_sync_init(void);
void time_sync_apply_epoch_ms(int64_t epoch_ms, int64_t uptime_ms);
int64_t time_sync_get_timestamp_ms(int64_t uptime_ms);
bool time_sync_is_synced(void);

#endif /* TIME_SYNC_H */
