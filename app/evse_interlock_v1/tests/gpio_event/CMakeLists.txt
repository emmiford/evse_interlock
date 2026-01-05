cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gpio_event_tests)

target_sources(app PRIVATE
	src/main.c
	../../src/gpio_event.c
)

target_include_directories(app PRIVATE
	../../include
)
