# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# YOUR BOARD HERE
set(ENV{BOARD} nrf52840dk/nrf52840)
#set(ENV{BOARD}BOARD=nrf52840dongle)

#set(ENV{ZEPHYR_BASE} D:/CLionWorkspaces/ble-framework/zephyr)

#find_package(Zephyr)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ble-framework-attacks)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})

zephyr_library_include_directories(${ZEPHYR_BASE}/samples/bluetooth)

# target_sources(app PRIVATE src/main.c)
