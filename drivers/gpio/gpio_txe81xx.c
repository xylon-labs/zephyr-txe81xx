/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ronny Eia */

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "txe_spi.h"

LOG_MODULE_REGISTER(gpio_txe81xx, CONFIG_GPIO_LOG_LEVEL);

#define TXE81XX_MAX_PORTS 3

struct gpio_txe81xx_config {
	/* Must be first */
	struct gpio_driver_config common;
	struct spi_dt_spec bus;
	struct gpio_dt_spec reset;
	struct gpio_dt_spec irq;
	uint8_t num_ports;
	uint32_t glitch_filter_mask;
};

struct gpio_txe81xx_data {
	/* Must be first */
	struct gpio_driver_data common;
	struct k_mutex lock;
	/* Cached register state per port */
	uint8_t output[TXE81XX_MAX_PORTS];
	uint8_t direction[TXE81XX_MAX_PORTS]; /* 1 = output, 0 = input */
	uint8_t pull_en[TXE81XX_MAX_PORTS];
	uint8_t pull_sel[TXE81XX_MAX_PORTS];    /* 1 = pull-up, 0 = pull-down */
	uint8_t input_cache[TXE81XX_MAX_PORTS]; /* last-read input state */
	uint8_t sir_cache; /* Smart Interrupt Register: bit n = port n SI disabled */
	/* IRQ */
	const struct device *dev; /* back-pointer for IRQ handler */
	struct gpio_callback irq_cb;
	struct k_work irq_work; /* deferred SPI work from ISR */
	sys_slist_t callbacks;
	/* Per-pin interrupt configuration */
	uint32_t int_enabled; /* bitmask of pins with interrupts enabled */
	uint32_t int_dual;    /* bitmask: both-edges mode */
	uint32_t int_rising;  /* bitmask: rising-edge / high-level */
	uint32_t int_falling; /* bitmask: falling-edge / low-level  */
};

/* -------------------------------------------------------------------------- */
/* Internal SPI helpers                                                        */
/* -------------------------------------------------------------------------- */

static int txe81xx_reg_write(const struct device *dev, uint8_t feature, uint8_t port, uint8_t value)
{
	const struct gpio_txe81xx_config *cfg = dev->config;
	uint8_t tx_buf[3];
	uint8_t rx_buf[3] = {0};

	const struct txe_spi_tx tx = {
		.read = false,
		.feature = feature,
		.port = port,
		.multi = false,
		.data = value,
	};

	txe_spi_pack(&tx, tx_buf);

	struct spi_buf spi_tx = {.buf = tx_buf, .len = 3};
	struct spi_buf spi_rx = {.buf = rx_buf, .len = 3};
	struct spi_buf_set tx_set = {.buffers = &spi_tx, .count = 1};
	struct spi_buf_set rx_set = {.buffers = &spi_rx, .count = 1};

	return spi_transceive_dt(&cfg->bus, &tx_set, &rx_set);
}

static int txe81xx_reg_read(const struct device *dev, uint8_t feature, uint8_t port, uint8_t *value)
{
	const struct gpio_txe81xx_config *cfg = dev->config;
	uint8_t tx_buf[3];
	uint8_t rx_buf[3] = {0};
	int ret;

	const struct txe_spi_tx tx = {
		.read = true,
		.feature = feature,
		.port = port,
		.multi = false,
		.data = 0,
	};

	txe_spi_pack(&tx, tx_buf);

	struct spi_buf spi_tx = {.buf = tx_buf, .len = 3};
	struct spi_buf spi_rx = {.buf = rx_buf, .len = 3};
	struct spi_buf_set tx_set = {.buffers = &spi_tx, .count = 1};
	struct spi_buf_set rx_set = {.buffers = &spi_rx, .count = 1};

	ret = spi_transceive_dt(&cfg->bus, &tx_set, &rx_set);
	if (ret) {
		return ret;
	}

	struct txe_spi_rx rx_frame;

	ret = txe_spi_unpack(rx_buf, &rx_frame);
	if (ret) {
		return ret;
	}

	*value = rx_frame.data;
	return 0;
}

/* -------------------------------------------------------------------------- */
/* GPIO driver API                                                             */
/* -------------------------------------------------------------------------- */

static int gpio_txe81xx_pin_configure(const struct device *dev, gpio_pin_t pin, gpio_flags_t flags)
{
	const struct gpio_txe81xx_config *cfg = dev->config;
	struct gpio_txe81xx_data *data = dev->data;
	uint8_t port = pin / 8;
	uint8_t bit = pin % 8;
	int ret;

	if (port >= cfg->num_ports) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (flags & GPIO_OUTPUT) {
		data->direction[port] |= BIT(bit);

		if (flags & GPIO_OUTPUT_INIT_HIGH) {
			data->output[port] |= BIT(bit);
		} else if (flags & GPIO_OUTPUT_INIT_LOW) {
			data->output[port] &= ~BIT(bit);
		}

		ret = txe81xx_reg_write(dev, TXE_FEATURE_OUTPUT_PORT, port, data->output[port]);
		if (ret) {
			goto out;
		}
	} else {
		data->direction[port] &= ~BIT(bit);
	}

	ret = txe81xx_reg_write(dev, TXE_FEATURE_DIRECTION, port, data->direction[port]);
	if (ret) {
		goto out;
	}

	if (flags & (GPIO_PULL_UP | GPIO_PULL_DOWN)) {
		data->pull_en[port] |= BIT(bit);
		if (flags & GPIO_PULL_UP) {
			data->pull_sel[port] |= BIT(bit);
		} else {
			data->pull_sel[port] &= ~BIT(bit);
		}
	} else {
		data->pull_en[port] &= ~BIT(bit);
	}

	ret = txe81xx_reg_write(dev, TXE_FEATURE_PULL_SELECT, port, data->pull_sel[port]);
	if (ret) {
		goto out;
	}

	ret = txe81xx_reg_write(dev, TXE_FEATURE_PULL_ENABLE, port, data->pull_en[port]);
out:
	k_mutex_unlock(&data->lock);
	return ret;
}

static int gpio_txe81xx_port_get_raw(const struct device *dev, gpio_port_value_t *value)
{
	const struct gpio_txe81xx_config *cfg = dev->config;
	struct gpio_txe81xx_data *data = dev->data;
	gpio_port_value_t result = 0;
	int ret = 0;

	k_mutex_lock(&data->lock, K_FOREVER);

	for (uint8_t port = 0; port < cfg->num_ports; port++) {
		uint8_t port_val;

		ret = txe81xx_reg_read(dev, TXE_FEATURE_INPUT_PORT, port, &port_val);
		if (ret) {
			goto out;
		}

		result |= (gpio_port_value_t)port_val << (port * 8);
	}

	*value = result;
out:
	k_mutex_unlock(&data->lock);
	return ret;
}

static int gpio_txe81xx_port_set_masked_raw(const struct device *dev, gpio_port_pins_t mask,
					    gpio_port_value_t value)
{
	const struct gpio_txe81xx_config *cfg = dev->config;
	struct gpio_txe81xx_data *data = dev->data;
	int ret = 0;

	k_mutex_lock(&data->lock, K_FOREVER);

	for (uint8_t port = 0; port < cfg->num_ports; port++) {
		uint8_t port_mask = (uint8_t)(mask >> (port * 8));
		uint8_t port_value = (uint8_t)(value >> (port * 8));

		if (!port_mask) {
			continue;
		}

		data->output[port] = (data->output[port] & ~port_mask) | (port_value & port_mask);

		ret = txe81xx_reg_write(dev, TXE_FEATURE_OUTPUT_PORT, port, data->output[port]);
		if (ret) {
			goto out;
		}
	}

out:
	k_mutex_unlock(&data->lock);
	return ret;
}

static int gpio_txe81xx_port_set_bits_raw(const struct device *dev, gpio_port_pins_t pins)
{
	return gpio_txe81xx_port_set_masked_raw(dev, pins, pins);
}

static int gpio_txe81xx_port_clear_bits_raw(const struct device *dev, gpio_port_pins_t pins)
{
	return gpio_txe81xx_port_set_masked_raw(dev, pins, 0);
}

static int gpio_txe81xx_port_toggle_bits(const struct device *dev, gpio_port_pins_t pins)
{
	const struct gpio_txe81xx_config *cfg = dev->config;
	struct gpio_txe81xx_data *data = dev->data;
	int ret = 0;

	k_mutex_lock(&data->lock, K_FOREVER);

	for (uint8_t port = 0; port < cfg->num_ports; port++) {
		uint8_t port_pins = (uint8_t)(pins >> (port * 8));

		if (!port_pins) {
			continue;
		}

		data->output[port] ^= port_pins;
		ret = txe81xx_reg_write(dev, TXE_FEATURE_OUTPUT_PORT, port, data->output[port]);
		if (ret) {
			goto out;
		}
	}

out:
	k_mutex_unlock(&data->lock);
	return ret;
}

static int gpio_txe81xx_pin_interrupt_configure(const struct device *dev, gpio_pin_t pin,
						enum gpio_int_mode mode, enum gpio_int_trig trig)
{
	const struct gpio_txe81xx_config *cfg = dev->config;
	struct gpio_txe81xx_data *data = dev->data;
	uint32_t bit = BIT(pin);

	if (pin >= (gpio_pin_t)(cfg->num_ports * 8)) {
		return -EINVAL;
	}

	if (cfg->irq.port == NULL) {
		return -ENOTSUP;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	data->int_enabled &= ~bit;
	data->int_dual &= ~bit;
	data->int_rising &= ~bit;
	data->int_falling &= ~bit;

	if (mode != GPIO_INT_MODE_DISABLED) {
		data->int_enabled |= bit;

		if (trig == GPIO_INT_TRIG_BOTH) {
			data->int_dual |= bit;
		} else if (trig == GPIO_INT_TRIG_HIGH) {
			data->int_rising |= bit;
		} else {
			data->int_falling |= bit;
		}
	}

	/* Sync IMR for this pin's port: 0=unmasked for enabled pins, 1=masked otherwise */
	uint8_t port = pin / 8;
	uint8_t imr_val = ~(uint8_t)(data->int_enabled >> (port * 8));
	int ret = txe81xx_reg_write(dev, TXE_FEATURE_IMR, port, imr_val);
	if (ret) {
		goto out;
	}

	/* Sync SIR for this port based on interrupt mode:
	 * LEVEL → SI enabled (bit=0): auto-clears when signal returns
	 * EDGE  → SI disabled (bit=1): only cleared by reading IFS
	 * Note: SIR is port-level; last configured pin on a port wins.
	 */
	if (mode == GPIO_INT_MODE_LEVEL) {
		data->sir_cache &= ~BIT(port);
	} else if (mode == GPIO_INT_MODE_EDGE) {
		data->sir_cache |= BIT(port);
	}
	ret = txe81xx_reg_write(dev, TXE_FEATURE_SIR, 0, data->sir_cache);

out:
	k_mutex_unlock(&data->lock);
	return ret;
}

static int gpio_txe81xx_manage_callback(const struct device *dev, struct gpio_callback *callback,
					bool set)
{
	struct gpio_txe81xx_data *data = dev->data;

	return gpio_manage_callback(&data->callbacks, callback, set);
}

/* Called in thread context via work queue — SPI is allowed here */
static void gpio_txe81xx_irq_work_handler(struct k_work *work)
{
	struct gpio_txe81xx_data *data = CONTAINER_OF(work, struct gpio_txe81xx_data, irq_work);
	const struct device *dev = data->dev;
	const struct gpio_txe81xx_config *cfg = dev->config;
	uint8_t ips;
	int ret;

	/*
	 * Drain all pending interrupt ports in a loop.  If a new edge arrives
	 * while this handler is running, k_work_submit() is a no-op on a
	 * running work item; looping until IPS is 0 ensures we catch edges
	 * that arrived during the current execution rather than losing them.
	 */
	for (;;) {
		ret = txe81xx_reg_read(dev, TXE_FEATURE_IPS, 0, &ips);
		if (ret) {
			LOG_ERR("IPS read failed: %d", ret);
			return;
		}

		if (!ips) {
			break;
		}

		uint32_t fired = 0;

		for (uint8_t port = 0; port < cfg->num_ports; port++) {
			if (!(ips & BIT(port))) {
				continue;
			}

			uint8_t ifs;
			uint8_t new_val;

			ret = txe81xx_reg_read(dev, TXE_FEATURE_IFS, port, &ifs);
			if (ret) {
				LOG_ERR("IFS read failed (port %u): %d", port, ret);
				continue;
			}

			ret = txe81xx_reg_read(dev, TXE_FEATURE_INPUT_PORT, port, &new_val);
			if (ret) {
				LOG_ERR("Input read failed (port %u): %d", port, ret);
				continue;
			}

			/*
			 * Only consider bits where the pin level actually
			 * changed from the cached state.  This suppresses
			 * double-fires caused by switch bounce: if the pin
			 * ends up in the same state as the cache, a second
			 * IFS entry for the same pin is discarded.
			 */
			uint8_t changed = data->input_cache[port] ^ new_val;

			data->input_cache[port] = new_val;

			for (uint8_t bit = 0; bit < 8; bit++) {
				if (!(ifs & BIT(bit))) {
					continue;
				}

				if (!(changed & BIT(bit))) {
					continue;
				}

				uint32_t pin_bit = BIT(port * 8 + bit);

				if (!(data->int_enabled & pin_bit)) {
					continue;
				}

				/*
				 * The TXE81xx has no hardware edge selection —
				 * the IRQ always fires on both edges.  Filter in
				 * software using the current pin level to match
				 * the configured trigger direction.
				 */
				bool rising = (new_val & BIT(bit)) != 0;

				if ((data->int_dual & pin_bit) ||
				    (rising && (data->int_rising & pin_bit)) ||
				    (!rising && (data->int_falling & pin_bit))) {
					fired |= pin_bit;
				}
			}
		}

		if (fired) {
			gpio_fire_callbacks(&data->callbacks, dev, fired);
		}
	}
}

/* Called in interrupt context — just wake the work queue */
static void gpio_txe81xx_irq_handler(const struct device *irq_dev, struct gpio_callback *cb,
				     gpio_port_pins_t pins)
{
	struct gpio_txe81xx_data *data = CONTAINER_OF(cb, struct gpio_txe81xx_data, irq_cb);

	ARG_UNUSED(irq_dev);
	ARG_UNUSED(pins);

	k_work_submit(&data->irq_work);
}

static const struct gpio_driver_api gpio_txe81xx_api = {
	.pin_configure = gpio_txe81xx_pin_configure,
	.port_get_raw = gpio_txe81xx_port_get_raw,
	.port_set_masked_raw = gpio_txe81xx_port_set_masked_raw,
	.port_set_bits_raw = gpio_txe81xx_port_set_bits_raw,
	.port_clear_bits_raw = gpio_txe81xx_port_clear_bits_raw,
	.port_toggle_bits = gpio_txe81xx_port_toggle_bits,
	.pin_interrupt_configure = gpio_txe81xx_pin_interrupt_configure,
	.manage_callback = gpio_txe81xx_manage_callback,
};

/* -------------------------------------------------------------------------- */
/* Initialization                                                              */
/* -------------------------------------------------------------------------- */

static int gpio_txe81xx_init(const struct device *dev)
{
	const struct gpio_txe81xx_config *cfg = dev->config;
	struct gpio_txe81xx_data *data = dev->data;
	uint8_t fault_status;
	int ret;

	k_mutex_init(&data->lock);
	data->dev = dev;
	k_work_init(&data->irq_work, gpio_txe81xx_irq_work_handler);

	if (!spi_is_ready_dt(&cfg->bus)) {
		LOG_ERR("SPI bus not ready");
		return -ENODEV;
	}

	if (cfg->reset.port != NULL) {
		if (!gpio_is_ready_dt(&cfg->reset)) {
			LOG_ERR("Reset GPIO not ready");
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&cfg->reset, GPIO_OUTPUT_INACTIVE);
		if (ret) {
			return ret;
		}

		/* Assert reset (active-low: logical 1 drives pin low) */
		ret = gpio_pin_set_dt(&cfg->reset, 1);
		if (ret) {
			return ret;
		}
		k_msleep(5);

		/* Release reset */
		ret = gpio_pin_set_dt(&cfg->reset, 0);
		if (ret) {
			return ret;
		}
		k_msleep(50);
	}

	/* Read fault status once — clears the POR flag */
	ret = txe81xx_reg_read(dev, TXE_FEATURE_FAULT_STATUS, 0, &fault_status);
	if (ret) {
		LOG_ERR("Failed to read fault status: %d", ret);
		return ret;
	}

	if (fault_status & TXE_FAULT_FAILSAFE) {
		LOG_ERR("Device in fail-safe mode (fault_status=0x%02x)", fault_status);
		return -EIO;
	}

	/* Mask all interrupt pins at boot; selectively unmasked by pin_interrupt_configure */
	for (uint8_t port = 0; port < cfg->num_ports; port++) {
		ret = txe81xx_reg_write(dev, TXE_FEATURE_IMR, port, 0xFF);
		if (ret) {
			LOG_ERR("IMR init failed (port %u): %d", port, ret);
			return ret;
		}
	}

	/* Enable smart interrupt on all ports at boot (auto-clears on signal return) */
	ret = txe81xx_reg_write(dev, TXE_FEATURE_SIR, 0, 0x00);
	if (ret) {
		LOG_ERR("SIR init failed: %d", ret);
		return ret;
	}
	data->sir_cache = 0x00;

	/* Seed input cache so the IRQ handler can detect changes */
	for (uint8_t port = 0; port < cfg->num_ports; port++) {
		txe81xx_reg_read(dev, TXE_FEATURE_INPUT_PORT, port, &data->input_cache[port]);
	}

	/* Apply glitch filter configuration from DT */
	if (cfg->glitch_filter_mask) {
		for (uint8_t port = 0; port < cfg->num_ports; port++) {
			uint8_t mask = (uint8_t)(cfg->glitch_filter_mask >> (port * 8));

			if (!mask) {
				continue;
			}

			ret = txe81xx_reg_write(dev, TXE_FEATURE_IGFE, port, mask);
			if (ret) {
				LOG_ERR("IGFE write failed (port %u): %d", port, ret);
				return ret;
			}
		}
	}

	/* Set up IRQ GPIO if present */
	if (cfg->irq.port != NULL) {
		if (!gpio_is_ready_dt(&cfg->irq)) {
			LOG_ERR("IRQ GPIO not ready");
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&cfg->irq, GPIO_INPUT);
		if (ret) {
			return ret;
		}

		gpio_init_callback(&data->irq_cb, gpio_txe81xx_irq_handler, BIT(cfg->irq.pin));

		ret = gpio_add_callback(cfg->irq.port, &data->irq_cb);
		if (ret) {
			return ret;
		}

		ret = gpio_pin_interrupt_configure_dt(&cfg->irq, GPIO_INT_EDGE_FALLING);
		if (ret) {
			return ret;
		}
	}

	LOG_DBG("Initialized (fault_status=0x%02x)", fault_status);
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Device instantiation — one macro for both chip variants                    */
/* -------------------------------------------------------------------------- */

#define GPIO_TXE81XX_INIT(n, variant)                                                              \
	static struct gpio_txe81xx_data gpio_txe81xx_data_##variant##_##n;                         \
                                                                                                   \
	static const struct gpio_txe81xx_config gpio_txe81xx_config_##variant##_##n = {            \
		.common =                                                                          \
			{                                                                          \
				.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_DT_INST(n),               \
			},                                                                         \
		.bus = SPI_DT_SPEC_INST_GET(n, SPI_WORD_SET(8) | SPI_TRANSFER_MSB),                \
		.reset = GPIO_DT_SPEC_INST_GET_OR(n, reset_gpios, {0}),                            \
		.irq = GPIO_DT_SPEC_INST_GET_OR(n, irq_gpios, {0}),                                \
		.num_ports = DT_INST_PROP(n, ngpios) / 8,                                          \
		.glitch_filter_mask = DT_INST_PROP_OR(n, glitch_filter_pins, 0),                   \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, gpio_txe81xx_init, NULL, &gpio_txe81xx_data_##variant##_##n,      \
			      &gpio_txe81xx_config_##variant##_##n, POST_KERNEL,                   \
			      CONFIG_GPIO_TXE81XX_INIT_PRIORITY, &gpio_txe81xx_api);

#define DT_DRV_COMPAT xylon_labs_txe8116
DT_INST_FOREACH_STATUS_OKAY_VARGS(GPIO_TXE81XX_INIT, txe8116)

#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT xylon_labs_txe8124
DT_INST_FOREACH_STATUS_OKAY_VARGS(GPIO_TXE81XX_INIT, txe8124)
