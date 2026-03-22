/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ronny Eia */

#include "txe_spi.h"

#include <errno.h>

void txe_spi_pack(const struct txe_spi_tx *tx, uint8_t out[3])
{
	out[0] = (uint8_t)((tx->read ? 0x80U : 0U) | (tx->feature & 0x1FU));
	out[1] = (uint8_t)(((tx->port & 0x07U) << 4) | (tx->multi ? 0x01U : 0U));
	out[2] = tx->data;
}

int txe_spi_unpack(const uint8_t in[3], struct txe_spi_rx *rx)
{
	if ((in[0] & 0xC0U) != 0xC0U) {
		return -EBADMSG;
	}

	rx->fault_status = in[0] & 0x3FU;
	rx->data = in[2];

	return 0;
}
