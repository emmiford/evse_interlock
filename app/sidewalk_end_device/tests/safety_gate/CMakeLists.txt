cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(safety_gate_tests)

target_sources(app PRIVATE
	src/main.c
	../../src/gpio_event.c
	../../src/safety_gate.c
)

target_include_directories(app PRIVATE
	../../include
	../../src
)
