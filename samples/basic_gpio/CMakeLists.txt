# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.20.0)

# Make the driver module available when building this sample standalone
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(basic_gpio)

target_sources(app PRIVATE src/main.c)
