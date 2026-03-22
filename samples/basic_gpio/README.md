# Basic GPIO Sample

This sample demonstrates basic usage of GPIO (General Purpose Input/Output) APIs in Zephyr on the TXE81xx platform.

## Overview

The sample shows how to configure and control GPIO pins, including setting pin direction, reading input values, and toggling outputs.

## Requirements

- Zephyr RTOS
- TXE81xx hardware or compatible board
- Properly configured Zephyr development environment

## Building and Running

1. Set up your Zephyr environment:
    ```sh
    source zephyr-env.sh
    ```
2. Build the sample:
    ```sh
    west build -b <your_board> samples/basic_gpio
    ```
3. Flash the sample to your board:
    ```sh
    west flash
    ```

## Expected Behavior

- The sample configures a GPIO pin as output and toggles its state.
- If supported, an LED or other output device will blink.

## References

- [Zephyr GPIO API documentation](https://docs.zephyrproject.org/latest/reference/peripherals/gpio.html)
- [TXE81xx Board Documentation](../boards/txe81xx/README.md)

## License

This sample is provided under the MIT license.
