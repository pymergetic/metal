# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

if(DEFINED ENV{ZEPHYR_BASE} AND NOT "$ENV{ZEPHYR_BASE}" STREQUAL "")
  set(PYMETAL_ZEPHYR_BASE $ENV{ZEPHYR_BASE})
else()
  get_filename_component(PYMETAL_WORKSPACE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
  set(PYMETAL_ZEPHYR_BASE "${PYMETAL_WORKSPACE_ROOT}/external/zephyr")
endif()

if(NOT EXISTS "${PYMETAL_ZEPHYR_BASE}/share/zephyr-package/cmake/ZephyrConfig.cmake")
  message(FATAL_ERROR "Zephyr not found at ${PYMETAL_ZEPHYR_BASE}; run west update from packages/metal")
endif()

find_package(Zephyr REQUIRED HINTS ${PYMETAL_ZEPHYR_BASE})
project(runtime)

set(PYMETAL_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

zephyr_include_directories(${PYMETAL_ROOT}/include)

set(PYMETAL_SOURCES
  ${PYMETAL_ROOT}/src/pymergetic/metal/memory/layout.c
  ${PYMETAL_ROOT}/src/pymergetic/metal/memory/boot.c
  ${PYMETAL_ROOT}/src/pymergetic/metal/port/zephyr/kernel_static_port.c
  ${PYMETAL_ROOT}/src/pymergetic/metal/port/zephyr/kernel_heap_port.c
  ${PYMETAL_ROOT}/src/pymergetic/metal/port/zephyr/malloc_heap_port.c
)

target_sources(app PRIVATE src/main.c ${PYMETAL_SOURCES})
