GPIO TXE81xx Driver
===================

Overview
--------

The ``gpio_txe81xx`` driver implements the Zephyr
`GPIO API <https://docs.zephyrproject.org/latest/hardware/peripherals/gpio.html>`_
for the Xylon Labs TXE81xx family of SPI GPIO expanders.

Supported devices:

- **TXE8116** — 16-bit GPIO expander
- **TXE8124** — 24-bit GPIO expander

Configuration
-------------

Enable the driver via Kconfig:

.. code-block:: kconfig

   CONFIG_GPIO_TXE81XX=y

Devicetree
----------

Add the device to your board overlay. Example for a TXE8116 on SPI1:

.. code-block:: dts

   &spi1 {
       txe81xx: txe81xx@0 {
           compatible = "xylon-labs,txe8116";
           reg = <0>;
           spi-max-frequency = <1000000>;
           ngpios = <16>;
           irq-gpios = <&gpioc 7 GPIO_ACTIVE_LOW>;
       };
   };

API
---

The driver exposes the standard Zephyr
`GPIO API <https://docs.zephyrproject.org/latest/hardware/peripherals/gpio.html>`_.
Refer to the Zephyr documentation for available functions.
