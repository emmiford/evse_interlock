#
# Copyright (c) 2023 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

# Zephyr CMake project
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sidewalk_sid_end_device)

# Source files
target_include_directories(app PRIVATE src)
target_sources(app PRIVATE
src/main/main.c
src/sidewalk/sidewalk.c
src/sidewalk/sidewalk_events.c
src/sidewalk/sidewalk_msg.c
src/sidewalk/time_sync.c
src/telemetry/gpio_event.c
src/telemetry/telemetry_gpio.c
src/telemetry/telemetry_evse.c
)

target_sources_ifdef(CONFIG_SID_END_DEVICE_EVSE_ENABLED app PRIVATE
    src/telemetry/evse.c
)

target_sources_ifdef(CONFIG_SIDEWALK_FILE_TRANSFER app PRIVATE 
    src/sidewalk/sbdt/scratch_buffer.c
)
target_sources_ifdef(CONFIG_SIDEWALK_FILE_TRANSFER_DFU app PRIVATE
    src/sidewalk/sbdt/dfu_file_transfer.c
)

target_sources(app PRIVATE
    src/main/app.c
    src/main/app_buttons.c
)

if(CONFIG_BT AND CONFIG_SIDEWALK_BLE)
    target_sources(app PRIVATE
        src/main/app_ble_auth.c
    )
endif()
