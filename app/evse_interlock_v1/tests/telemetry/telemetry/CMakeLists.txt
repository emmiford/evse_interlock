cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(telemetry_tests)

target_sources(app PRIVATE
	src/main.c
	../../../src/telemetry/telemetry_gpio.c
	../../../src/telemetry/telemetry_evse.c
	../../../src/telemetry/gpio_event.c
)

target_include_directories(app PRIVATE
	../../../src
)
