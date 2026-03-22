/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ronny Eia */

#include <zephyr/ztest.h>
#include "txe_spi.h"

/* -------------------------------------------------------------------------- */
/* txe_spi_pack                                                          */
/* -------------------------------------------------------------------------- */

ZTEST(txe_spi, test_pack_write)
{
	/* Write to OUTPUT_PORT (0x03), port 1, data 0xAB */
	const struct txe_spi_tx tx = {
		.read = false,
		.feature = TXE_FEATURE_OUTPUT_PORT,
		.port = 1,
		.multi = false,
		.data = 0xAB,
	};
	uint8_t buf[3];

	txe_spi_pack(&tx, buf);

	/* Byte 0: read=0, feature=0x03 → 0x03 */
	zassert_equal(buf[0], 0x03, "byte0: got 0x%02x", buf[0]);
	/* Byte 1: port=1 << 4, multi=0 → 0x10 */
	zassert_equal(buf[1], 0x10, "byte1: got 0x%02x", buf[1]);
	/* Byte 2: data */
	zassert_equal(buf[2], 0xAB, "byte2: got 0x%02x", buf[2]);
}

ZTEST(txe_spi, test_pack_read)
{
	/* Read from INPUT_PORT (0x02), port 0 */
	const struct txe_spi_tx tx = {
		.read = true,
		.feature = TXE_FEATURE_INPUT_PORT,
		.port = 0,
		.multi = false,
		.data = 0,
	};
	uint8_t buf[3];

	txe_spi_pack(&tx, buf);

	/* Byte 0: read=1 → bit7 set, feature=0x02 → 0x82 */
	zassert_equal(buf[0], 0x82, "byte0: got 0x%02x", buf[0]);
	zassert_equal(buf[1], 0x00, "byte1: got 0x%02x", buf[1]);
	zassert_equal(buf[2], 0x00, "byte2: got 0x%02x", buf[2]);
}

ZTEST(txe_spi, test_pack_multi)
{
	const struct txe_spi_tx tx = {
		.read = false,
		.feature = TXE_FEATURE_DIRECTION,
		.port = 2,
		.multi = true,
		.data = 0xFF,
	};
	uint8_t buf[3];

	txe_spi_pack(&tx, buf);

	zassert_equal(buf[0], 0x04, "byte0: got 0x%02x", buf[0]);
	/* port=2 << 4 = 0x20, multi=1 → 0x21 */
	zassert_equal(buf[1], 0x21, "byte1: got 0x%02x", buf[1]);
	zassert_equal(buf[2], 0xFF, "byte2: got 0x%02x", buf[2]);
}

ZTEST(txe_spi, test_pack_feature_masked_to_5bits)
{
	/* Feature value with high bits set — only low 5 bits should survive */
	const struct txe_spi_tx tx = {
		.read = false,
		.feature = 0xFF, /* only 0x1F survives masking */
		.port = 0,
		.multi = false,
		.data = 0,
	};
	uint8_t buf[3];

	txe_spi_pack(&tx, buf);

	zassert_equal(buf[0] & 0x1F, 0x1F, "feature bits: got 0x%02x", buf[0]);
	/* read bit must be 0 */
	zassert_equal(buf[0] & 0x80, 0, "read bit should be clear");
}

/* -------------------------------------------------------------------------- */
/* txe_spi_unpack                                                        */
/* -------------------------------------------------------------------------- */

ZTEST(txe_spi, test_unpack_valid)
{
	/* Valid RX frame: byte0 top 2 bits = 0b11, fault_status=0x05, data=0x42 */
	const uint8_t raw[3] = {0xC5, 0x00, 0x42};
	struct txe_spi_rx rx;

	int ret = txe_spi_unpack(raw, &rx);

	zassert_equal(ret, 0, "expected 0, got %d", ret);
	zassert_equal(rx.fault_status, 0x05, "fault_status: got 0x%02x", rx.fault_status);
	zassert_equal(rx.data, 0x42, "data: got 0x%02x", rx.data);
}

ZTEST(txe_spi, test_unpack_no_fault)
{
	/* Normal clean response: fault_status=0, data=0xBE */
	const uint8_t raw[3] = {0xC0, 0x00, 0xBE};
	struct txe_spi_rx rx;

	int ret = txe_spi_unpack(raw, &rx);

	zassert_equal(ret, 0, "expected 0, got %d", ret);
	zassert_equal(rx.fault_status, 0x00, "expected no fault");
	zassert_equal(rx.data, 0xBE, "data: got 0x%02x", rx.data);
}

ZTEST(txe_spi, test_unpack_invalid_magic)
{
	/* Top 2 bits are not 0b11 — must return -EBADMSG */
	const uint8_t raw[3] = {0x80, 0x00, 0x00}; /* only bit7 set */
	struct txe_spi_rx rx;

	int ret = txe_spi_unpack(raw, &rx);

	zassert_equal(ret, -EBADMSG, "expected -EBADMSG, got %d", ret);
}

ZTEST(txe_spi, test_unpack_zero_invalid)
{
	const uint8_t raw[3] = {0x00, 0x00, 0x00};
	struct txe_spi_rx rx;

	int ret = txe_spi_unpack(raw, &rx);

	zassert_equal(ret, -EBADMSG, "expected -EBADMSG, got %d", ret);
}

ZTEST_SUITE(txe_spi, NULL, NULL, NULL, NULL, NULL);
