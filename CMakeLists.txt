# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# YOUR BOARD HERE
set(ENV{BOARD} nrf52840dk/nrf52840)
#set(ENV{BOARD} nrf5340dk/nrf5340/cpuapp)
#set(ENV{BOARD} nrf5340dk/nrf5340/cpunet)
# set(ENV{BOARD} nrf54l15dk/nrf54l15/cpuapp)

#find_package(Zephyr)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ble-framework)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})

zephyr_include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

zephyr_include_directories_ifdef(
        CONFIG_SOC_COMPATIBLE_NRF53X
        ${CMAKE_CURRENT_SOURCE_DIR}/common_nRF5340/include
)