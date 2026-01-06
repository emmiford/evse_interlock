/*
 * Safety gate ztests (native_posix)
 */
#include <zephyr/ztest.h>
#include <zephyr/sys/util.h>

#include "safety_gate/safety_gate.h"
#include "sidewalk/time_sync.h"

#if IS_ENABLED(CONFIG_LOG)
#error "CONFIG_LOG must be disabled for safety_gate tests"
#endif

ZTEST(safety_gate, test_ac_on_at_boot)
{
	struct safety_gate gate;

	safety_gate_init(&gate, 50);
	safety_gate_update_ac(&gate, 1, 0);
	zassert_false(safety_gate_is_ev_allowed(&gate), NULL);
}

ZTEST(safety_gate, test_ac_toggle_debounce)
{
	struct safety_gate gate;

	safety_gate_init(&gate, 50);
	safety_gate_update_ac(&gate, 0, 0);
	safety_gate_update_ac(&gate, 1, 10);
	zassert_false(safety_gate_is_ev_allowed(&gate), NULL);
	safety_gate_update_ac(&gate, 0, 20);
	zassert_false(safety_gate_is_ev_allowed(&gate), NULL);
}

ZTEST(safety_gate, test_ac_unknown)
{
	struct safety_gate gate;

	safety_gate_init(&gate, 50);
	safety_gate_update_ac(&gate, -1, 10);
	zassert_false(safety_gate_is_ev_allowed(&gate), NULL);
	zassert_true(safety_gate_has_fault(&gate, SAFETY_FAULT_AC_UNKNOWN), NULL);
}

ZTEST(safety_gate, test_timestamp_backward)
{
	struct safety_gate gate;
	int64_t ts;

	safety_gate_init(&gate, 50);
	(void)safety_gate_apply_timestamp(&gate, 1000);
	ts = safety_gate_apply_timestamp(&gate, 900);
	zassert_equal(ts, 1000, NULL);
	zassert_false(safety_gate_is_ev_allowed(&gate), NULL);
	zassert_true(safety_gate_has_fault(&gate, SAFETY_FAULT_TIMESTAMP_BACKWARD), NULL);
	zassert_true(safety_gate_time_anomaly(&gate), NULL);
}

ZTEST(safety_gate, test_queue_overflow)
{
	struct safety_gate gate;

	safety_gate_init(&gate, 50);
	safety_gate_set_queue_overflow(&gate);
	zassert_false(safety_gate_is_ev_allowed(&gate), NULL);
	zassert_true(safety_gate_has_fault(&gate, SAFETY_FAULT_QUEUE_OVERFLOW), NULL);
}

ZTEST(safety_gate, test_invalid_debounce)
{
	struct safety_gate gate;

	safety_gate_init(&gate, 0);
	zassert_false(safety_gate_is_ev_allowed(&gate), NULL);
	zassert_true(safety_gate_has_fault(&gate, SAFETY_FAULT_DEBOUNCE_INVALID), NULL);
}

ZTEST(safety_gate, test_time_sync_backward_clamp)
{
	int64_t ts;

	time_sync_init();
	ts = time_sync_get_timestamp_ms(1000);
	zassert_equal(ts, 1000, NULL);
	zassert_false(time_sync_time_anomaly(), NULL);

	time_sync_apply_epoch_ms(500, 1000);
	ts = time_sync_get_timestamp_ms(1100);
	zassert_equal(ts, 1100, NULL);
	zassert_true(time_sync_time_anomaly(), NULL);

	ts = time_sync_get_timestamp_ms(1200);
	zassert_equal(ts, 1200, NULL);
}

ZTEST_SUITE(safety_gate, NULL, NULL, NULL, NULL, NULL);
