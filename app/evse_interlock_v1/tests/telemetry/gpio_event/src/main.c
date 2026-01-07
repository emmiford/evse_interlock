/*
 * [TEST] GPIO event unit tests (host).
 * [BOILERPLATE] Debounce/edge detection harness validation.
 */
#include <zephyr/ztest.h>
#include "telemetry/gpio_event.h"

ZTEST(gpio_event, test_debounce)
{
	/* [BOILERPLATE] Debounce timing behavior. */
	struct gpio_event_state st;
	bool changed = false;

	gpio_event_init(&st, 50);
	zassert_equal(gpio_event_update(&st, 0, 0, &changed), GPIO_EDGE_NONE, NULL);
	zassert_false(changed, NULL);

	zassert_equal(gpio_event_update(&st, 1, 10, &changed), GPIO_EDGE_NONE, NULL);
	zassert_false(changed, NULL);

	zassert_equal(gpio_event_update(&st, 1, 40, &changed), GPIO_EDGE_NONE, NULL);
	zassert_false(changed, NULL);

	zassert_equal(gpio_event_update(&st, 1, 70, &changed), GPIO_EDGE_RISING, NULL);
	zassert_true(changed, NULL);
}

ZTEST(gpio_event, test_edge_detection)
{
	/* [BOILERPLATE] Edge transitions from debounced state changes. */
	struct gpio_event_state st;
	bool changed = false;

	gpio_event_init(&st, 0);
	(void)gpio_event_update(&st, 0, 0, &changed);

	zassert_equal(gpio_event_update(&st, 1, 1, &changed), GPIO_EDGE_NONE, NULL);
	zassert_false(changed, NULL);
	zassert_equal(gpio_event_update(&st, 1, 1, &changed), GPIO_EDGE_RISING, NULL);
	zassert_true(changed, NULL);

	zassert_equal(gpio_event_update(&st, 0, 2, &changed), GPIO_EDGE_NONE, NULL);
	zassert_false(changed, NULL);
	zassert_equal(gpio_event_update(&st, 0, 2, &changed), GPIO_EDGE_FALLING, NULL);
	zassert_true(changed, NULL);
}

ZTEST(gpio_event, test_no_spam_same_state)
{
	/* [BOILERPLATE] No repeat edge when state is stable. */
	struct gpio_event_state st;
	bool changed = false;

	gpio_event_init(&st, 0);
	(void)gpio_event_update(&st, 1, 0, &changed);
	zassert_equal(gpio_event_update(&st, 1, 1, &changed), GPIO_EDGE_NONE, NULL);
	zassert_false(changed, NULL);
}

ZTEST_SUITE(gpio_event, NULL, NULL, NULL, NULL, NULL);
