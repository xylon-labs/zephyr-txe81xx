.. _txe81xx_gpio_interrupt_sample:

TXE81xx GPIO Interrupt Sample
##############################

Overview
********

Demonstrates interrupt-driven GPIO input using the TXE81xx I/O expander driver.
A switch connected to an expander input pin generates edge interrupts; the
handler logs the pin state and toggles an LED on each event.

Requirements
************

* A TXE81xx I/O expander (TXE8116 or TXE8132) connected via SPI.
* An LED connected to expander port 0, pin 0 (active low).
* A momentary switch connected to expander port 1, pin 0 (active high, pulled
  low at rest).
* A board overlay providing the ``txe81xx`` DT node with ``led-gpios`` and
  ``sw-gpios`` properties.

Wiring (Nucleo-L476RG)
**********************

+-------------------+----------------+
| TXE81xx signal    | Nucleo pin     |
+===================+================+
| SPI SCK           | PA5 (SPI1)     |
+-------------------+----------------+
| SPI MISO          | PA6 (SPI1)     |
+-------------------+----------------+
| SPI MOSI          | PA7 (SPI1)     |
+-------------------+----------------+
| SPI CS            | PA4            |
+-------------------+----------------+
| RESET#            | PC6            |
+-------------------+----------------+
| IRQ#              | PC7            |
+-------------------+----------------+

Building
********

.. code-block:: bash

   west build -b nucleo_l476rg samples/gpio_interrupt --pristine

Flashing
********

.. code-block:: bash

   west flash

Sample Output
*************

.. code-block:: console

   *** Booting Zephyr OS build v4.3.0 ***
   [00:00:00.061,000] <inf> gpio_interrupt: TXE81xx GPIO interrupt sample
   [00:00:00.068,000] <inf> gpio_interrupt: Waiting for switch events...
   [00:00:03.402,000] <inf> gpio_interrupt: Switch: PRESSED
   [00:00:03.626,000] <inf> gpio_interrupt: Switch: RELEASED
