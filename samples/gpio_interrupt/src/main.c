/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ronny Eia */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(gpio_interrupt, LOG_LEVEL_INF);

#define TXE_NODE DT_NODELABEL(txe81xx)

#if !DT_NODE_EXISTS(TXE_NODE)
#error "DT node 'txe81xx' not found — add a board overlay with the TXE81xx node"
#endif

/*
 * Pins are defined in the board overlay:
 *   led-gpios: port 0, pin 0 — toggled on each switch event
 *   sw-gpios:  port 1, pin 0 — triggers the interrupt
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(TXE_NODE, led_gpios);
static const struct gpio_dt_spec sw = GPIO_DT_SPEC_GET(TXE_NODE, sw_gpios);

static struct gpio_callback sw_cb;

static void sw_handler(const struct device *dev, struct gpio_callback *cb, gpio_port_pins_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	int state = gpio_pin_get_dt(&sw);

	LOG_INF("Switch: %s", state > 0 ? "PRESSED" : "RELEASED");
	gpio_pin_toggle_dt(&led);
}

int main(void)
{
	int ret;

	LOG_INF("TXE81xx GPIO interrupt sample");

	if (!gpio_is_ready_dt(&led) || !gpio_is_ready_dt(&sw)) {
		LOG_ERR("GPIO not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (ret) {
		LOG_ERR("Failed to configure LED: %d", ret);
		return ret;
	}

	ret = gpio_pin_configure_dt(&sw, GPIO_INPUT | GPIO_PULL_DOWN);
	if (ret) {
		LOG_ERR("Failed to configure switch: %d", ret);
		return ret;
	}

	gpio_init_callback(&sw_cb, sw_handler, BIT(sw.pin));

	ret = gpio_add_callback_dt(&sw, &sw_cb);
	if (ret) {
		LOG_ERR("Failed to add callback: %d", ret);
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&sw, GPIO_INT_EDGE_BOTH);
	if (ret) {
		LOG_ERR("Failed to configure interrupt: %d", ret);
		return ret;
	}

	LOG_INF("Waiting for switch events...");

	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
