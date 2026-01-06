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

if(CONFIG_SID_END_DEVICE_DUT)
    target_sources(app PRIVATE
        src/test_mode/app.c
    )
else()
    target_sources(app PRIVATE
        src/main/app.c
    )
endif()

if(CONFIG_SID_END_DEVICE_CLI)
    target_sources(app PRIVATE
        src/test_mode/app_dut.c
        src/test_mode/app_shell.c
    )
    target_sources_ifdef(CONFIG_SIDEWALK_FILE_TRANSFER_SHELL app PRIVATE
        src/test_mode/sbdt_shell.c
        src/test_mode/sbdt_shell_events.c
        src/test_mode/sbdt_shell_file_transfer.c
    )
    target_sources_ifdef(CONFIG_SIDEWALK_ON_DEV_CERT app PRIVATE 
        src/test_mode/sid_on_dev_cert_cli.c
    )
    target_sources_ifdef(CONFIG_SIDEWALK_LOCATION_SHELL app PRIVATE
        src/test_mode/location_shell.c
        src/test_mode/location_shell_events.c
    )
endif()
