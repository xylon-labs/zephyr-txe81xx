/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ronny Eia */

/*
 * Unit tests for the software edge filter used in gpio_txe81xx_irq_work_handler.
 *
 * The logic under test (reproduced from the driver):
 *
 *   uint8_t changed = input_cache ^ new_val;
 *
 *   for each bit set in ifs:
 *     if !(changed & bit)  → skip (pin didn't actually change → bounce duplicate)
 *     if !(int_enabled & pin_bit) → skip
 *     rising = (new_val & bit) != 0
 *     fire if: int_dual  OR
 *              (rising  AND int_rising)  OR
 *              (!rising AND int_falling)
 */

#include <zephyr/ztest.h>
#include <stdint.h>
#include <stdbool.h>

/* Mirrors the filter logic from the driver work handler for a single bit */
static bool edge_filter(uint8_t cache, uint8_t new_val, uint8_t bit, uint32_t int_enabled,
			uint32_t int_dual, uint32_t int_rising, uint32_t int_falling, uint8_t port)
{
	uint8_t changed = cache ^ new_val;
	uint32_t pin_bit = BIT(port * 8 + bit);

	if (!(changed & BIT(bit))) {
		return false; /* no real level change */
	}

	if (!(int_enabled & pin_bit)) {
		return false; /* interrupt not configured for this pin */
	}

	bool rising = (new_val & BIT(bit)) != 0;

	return (int_dual & pin_bit) || (rising && (int_rising & pin_bit)) ||
	       (!rising && (int_falling & pin_bit));
}

/* -------------------------------------------------------------------------- */
/* EDGE_BOTH                                                                  */
/* -------------------------------------------------------------------------- */

ZTEST(edge_filter, test_both_rising_fires)
{
	/* pin 0, port 0: was 0, now 1 → rising, EDGE_BOTH → fire */
	zassert_true(edge_filter(0x00, 0x01, 0, BIT(0), BIT(0), 0, 0, 0));
}

ZTEST(edge_filter, test_both_falling_fires)
{
	/* pin 0, port 0: was 1, now 0 → falling, EDGE_BOTH → fire */
	zassert_true(edge_filter(0x01, 0x00, 0, BIT(0), BIT(0), 0, 0, 0));
}

/* -------------------------------------------------------------------------- */
/* EDGE_TO_ACTIVE (rising only)                                               */
/* -------------------------------------------------------------------------- */

ZTEST(edge_filter, test_rising_only_rising_fires)
{
	/* was 0, now 1 → rising, int_rising set → fire */
	zassert_true(edge_filter(0x00, 0x01, 0, BIT(0), 0, BIT(0), 0, 0));
}

ZTEST(edge_filter, test_rising_only_falling_suppressed)
{
	/* was 1, now 0 → falling, int_rising set only → suppress */
	zassert_false(edge_filter(0x01, 0x00, 0, BIT(0), 0, BIT(0), 0, 0));
}

/* -------------------------------------------------------------------------- */
/* EDGE_TO_INACTIVE (falling only)                                            */
/* -------------------------------------------------------------------------- */

ZTEST(edge_filter, test_falling_only_falling_fires)
{
	/* was 1, now 0 → falling, int_falling set → fire */
	zassert_true(edge_filter(0x01, 0x00, 0, BIT(0), 0, 0, BIT(0), 0));
}

ZTEST(edge_filter, test_falling_only_rising_suppressed)
{
	/* was 0, now 1 → rising, int_falling set only → suppress */
	zassert_false(edge_filter(0x00, 0x01, 0, BIT(0), 0, 0, BIT(0), 0));
}

/* -------------------------------------------------------------------------- */
/* Bounce deduplication                                                       */
/* -------------------------------------------------------------------------- */

ZTEST(edge_filter, test_no_level_change_suppressed)
{
	/* IFS says pin fired but new_val == cache → bounce, suppress */
	zassert_false(edge_filter(0x01, 0x01, 0, BIT(0), BIT(0), 0, 0, 0));
}

ZTEST(edge_filter, test_different_pin_not_affected)
{
	/* pin 1 changed, pin 0 did not — pin 0 must not fire */
	zassert_false(edge_filter(0x00, 0x02, 0 /* bit 0 */, BIT(0), BIT(0), 0, 0, 0));
}

/* -------------------------------------------------------------------------- */
/* Interrupt not enabled                                                      */
/* -------------------------------------------------------------------------- */

ZTEST(edge_filter, test_int_not_enabled_suppressed)
{
	/* Level changed but int_enabled is 0 → suppress */
	zassert_false(edge_filter(0x00, 0x01, 0, 0 /* int_enabled=0 */, BIT(0), 0, 0, 0));
}

/* -------------------------------------------------------------------------- */
/* Multi-port: pin on port 1                                                  */
/* -------------------------------------------------------------------------- */

ZTEST(edge_filter, test_port1_pin0_rising_fires)
{
	/* port 1, bit 0 → absolute pin 8, EDGE_BOTH */
	zassert_true(edge_filter(0x00, 0x01, 0, BIT(8), BIT(8), 0, 0, 1));
}

ZTEST(edge_filter, test_port1_pin0_wrong_port_ignored)
{
	/* pin bit computed with port=0 instead of port=1 → wrong pin_bit → no match */
	zassert_false(edge_filter(0x00, 0x01, 0, BIT(8), BIT(8), 0, 0, 0 /* wrong port */));
}

ZTEST_SUITE(edge_filter, NULL, NULL, NULL, NULL, NULL);
