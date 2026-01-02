/*
 * GPIO event unit tests (host)
 */
#include <zephyr/ztest.h>
#include <string.h>

#include "gpio_event.h"

ZTEST(gpio_event, test_debounce)
{
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
	struct gpio_event_state st;
	bool changed = false;

	gpio_event_init(&st, 0);
	(void)gpio_event_update(&st, 1, 0, &changed);
	zassert_equal(gpio_event_update(&st, 1, 1, &changed), GPIO_EDGE_NONE, NULL);
	zassert_false(changed, NULL);
}

ZTEST(gpio_event, test_payload_builder)
{
	char buf[160];
	int len = gpio_event_build_payload(buf, sizeof(buf), "extinput0", 1,
					   GPIO_EDGE_RISING, 1234, "abcd1234");
	zassert_true(len > 0, NULL);
	zassert_not_null(strstr(buf, "\"source\":\"gpio\""), NULL);
	zassert_not_null(strstr(buf, "\"pin\":\"extinput0\""), NULL);
	zassert_not_null(strstr(buf, "\"state\":1"), NULL);
	zassert_not_null(strstr(buf, "\"edge\":\"rising\""), NULL);
	zassert_not_null(strstr(buf, "\"run_id\":\"abcd1234\""), NULL);
}

ZTEST(gpio_event, test_payload_bounds)
{
	char buf[16];
	int len = gpio_event_build_payload(buf, sizeof(buf), "extinput0", 1,
					   GPIO_EDGE_RISING, 1234, "abcd1234");
	zassert_equal(len, -1, NULL);
}

ZTEST_SUITE(gpio_event, NULL, NULL, NULL, NULL, NULL);
