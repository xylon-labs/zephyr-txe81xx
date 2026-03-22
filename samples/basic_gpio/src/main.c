/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ronny Eia */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(basic_gpio, LOG_LEVEL_INF);

#define NODE DT_NODELABEL(txe81xx)

#if !DT_NODE_EXISTS(NODE)
#error "DT node 'txe81xx' not found — add a board overlay with the TXE81xx node"
#endif

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(NODE, led_gpios);
static const struct gpio_dt_spec btn = GPIO_DT_SPEC_GET(NODE, sw_gpios);

int main(void)
{
	int ret;

	LOG_INF("Basic GPIO sample start");

	if (!gpio_is_ready_dt(&led) || !gpio_is_ready_dt(&btn)) {
		LOG_ERR("GPIO not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (ret) {
		LOG_ERR("Failed to configure LED: %d", ret);
		return ret;
	}

	ret = gpio_pin_configure_dt(&btn, GPIO_INPUT | GPIO_PULL_DOWN);
	if (ret) {
		LOG_ERR("Failed to configure button: %d", ret);
		return ret;
	}

	while (1) {
		int val = gpio_pin_get_dt(&btn);
		gpio_pin_set_dt(&led, val);
		k_msleep(50);
	}
}
