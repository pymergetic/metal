# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
project(pm_linux_bench C)

set(PYMETAL_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)

add_executable(pm-linux-bench
  main.c
  ${PYMETAL_ROOT}/src/pymergetic/metal/memory/bench.c
  ${PYMETAL_ROOT}/src/pymergetic/port/linux/bench_port.c
)

target_include_directories(pm-linux-bench PRIVATE
  ${PYMETAL_ROOT}/include
  ${PYMETAL_ROOT}/src/pymergetic/port/headers
)

target_compile_definitions(pm-linux-bench PRIVATE PM_MAX_VIS=1)

target_compile_options(pm-linux-bench PRIVATE -Wall -Wextra -O2)
