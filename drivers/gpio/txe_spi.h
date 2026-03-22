/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ronny Eia */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Feature register addresses (5-bit, used in txe_spi_tx.feature) */
#define TXE_FEATURE_DEVICE_ID    0x01U
#define TXE_FEATURE_INPUT_PORT   0x02U /* Read-only  */
#define TXE_FEATURE_OUTPUT_PORT  0x03U /* Read/write */
#define TXE_FEATURE_DIRECTION    0x04U /* Read/write: 0=input, 1=output */
#define TXE_FEATURE_PULL_ENABLE  0x08U /* Read/write: 0=disabled, 1=enabled */
#define TXE_FEATURE_PULL_SELECT  0x09U /* Read/write: 0=pull-down, 1=pull-up */
#define TXE_FEATURE_SIR          0x0BU /* Read/write: Smart Interrupt Register       */
				       /*   bit n = port n: 0=SI enabled, 1=disabled  */
				       /*   SI enabled:  interrupt auto-clears when   */
				       /*     input returns to initial state           */
				       /*   SI disabled: interrupt only cleared by     */
				       /*     reading IFS                              */
				       /*   bits 3-7: reserved                        */
#define TXE_FEATURE_IMR          0x0CU /* Read/write: Interrupt Mask Register        */
				       /*   0 = not masked (interrupt fires on change) */
				       /*   1 = masked (interrupt suppressed)          */
#define TXE_FEATURE_IGFE         0x0DU /* Read/write: Interrupt Glitch Filter Enable */
				       /*   bit n = pin n glitch filter enabled      */
#define TXE_FEATURE_IFS          0x0EU /* Read-only:  Interrupt Flag Status   */
				       /*   bit n = pin n caused the interrupt */
#define TXE_FEATURE_IPS          0x0FU /* Read-only:  Interrupt Port Status   */
				       /*   bit 0 = IPS.P0 .. bit 2 = IPS.P2 */
				       /*   bits 3-7: reserved                */
#define TXE_FEATURE_FAULT_STATUS 0x19U

/* Port numbers (3-bit, used in txe_spi_tx.port) */
#define TXE_PORT_0 0x00U
#define TXE_PORT_1 0x01U
#define TXE_PORT_2 0x02U

/* Device ID values (txe_spi_rx.data when feature == TXE_FEATURE_DEVICE_ID) */
#define TXE_DEVICE_ID_TXE8116 0x00U
#define TXE_DEVICE_ID_TXE8124 0x01U

/* Fault status register bits (txe_spi_rx.data when feature == TXE_FEATURE_FAULT_STATUS) */
#define TXE_FAULT_POR        (1U << 0) /* Power-on reset recovery      */
#define TXE_FAULT_REDUNDANCY (1U << 1) /* Redundancy register mismatch */
#define TXE_FAULT_FAILSAFE   (1U << 2) /* Device in fail-safe mode     */
/* Bits 7-3: reserved */

/**
 * @brief TXE SPI transmit frame.
 *
 * Wire layout (24-bit, MSB first):
 *
 * Byte 0 (B23-16): [ B23: read=1 for read, 0 for write ] [ B22: always 0 ] [ B21: reserved, set to
 * 0 ] [ B20-16: feature address (5 bits) ] Byte 1 (B15-8):  [ B15: reserved, set to 0 ] [ B14-B12:
 * port number (3 bits) ] [ B11-B9: reserved, set to 0 ] [ B8: multi=1 for multi-port ops, 0 for
 * single-port ] Byte 2 (B7-0):   [ data byte for write operations, ignored for read ]
 */
struct txe_spi_tx {
	bool read;       /**< true = read transaction, false = write */
	uint8_t feature; /**< 5-bit feature/register address */
	uint8_t port;    /**< 3-bit target port number (0–2) */
	bool multi;      /**< true = multi-port burst, false = single-port */
	uint8_t data;    /**< Data byte to write; ignored for read transactions */
};

/**
 * @brief Decoded RX frame received from the TXE81xx device.
 *
 * Wire layout (24-bit, MSB first):
 *
 *  Byte 0 (B23-16): [ B23-22: always 1 ] [ B21-16: fault_status ]
 *  Byte 1 (B15-8):  [ reserved, always 0x00 ]
 *  Byte 2 (B7-0):   [ data ]
 */
struct txe_spi_rx {
	uint8_t fault_status; /**< 6-bit fault/status flags from B21–B16 (see TXE_FAULT_*) */
	uint8_t data;         /**< 8-bit response data byte from B7–B0 */
};

/**
 * @brief Encode a TX frame into a 3-byte SPI wire buffer.
 *
 * @param tx  Pointer to the frame descriptor to encode.
 * @param out Output buffer of exactly 3 bytes (MSB first).
 */
void txe_spi_pack(const struct txe_spi_tx *tx, uint8_t out[3]);

/**
 * @brief Decode a 3-byte SPI response into an RX frame.
 *
 * Validates the two magic bits (B23–B22 must both be 1). Returns
 * -EBADMSG if the magic check fails, leaving @p rx unmodified.
 *
 * @param in  Input buffer of exactly 3 bytes received from the device.
 * @param rx  Output frame populated on success.
 *
 * @retval 0         Success.
 * @retval -EBADMSG  Invalid frame magic — not a TXE81xx response.
 */
int txe_spi_unpack(const uint8_t in[3], struct txe_spi_rx *rx);
