# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

if(DEFINED ENV{ZEPHYR_BASE} AND NOT "$ENV{ZEPHYR_BASE}" STREQUAL "")
  set(PYMETAL_ZEPHYR_BASE $ENV{ZEPHYR_BASE})
else()
  get_filename_component(PYMETAL_WORKSPACE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
  set(PYMETAL_ZEPHYR_BASE "${PYMETAL_WORKSPACE_ROOT}/external/zephyr")
endif()

if(NOT EXISTS "${PYMETAL_ZEPHYR_BASE}/share/zephyr-package/cmake/ZephyrConfig.cmake")
  message(FATAL_ERROR "Zephyr not found at ${PYMETAL_ZEPHYR_BASE}; run west update from packages/metal")
endif()

find_package(Zephyr REQUIRED HINTS ${PYMETAL_ZEPHYR_BASE})
project(runtime)

set(PYMETAL_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(TLSF_DIR ${PYMETAL_ROOT}/external/tlsf)

zephyr_include_directories(${PYMETAL_ROOT}/include)
zephyr_include_directories(${PYMETAL_ROOT}/src/pymergetic/port/headers)
zephyr_include_directories(${PYMETAL_ROOT}/src/pymergetic/metal/util)

zephyr_compile_definitions(PM_MAX_VIS=1)

set(PYMETAL_PORT_SOURCES
  ${PYMETAL_ROOT}/src/pymergetic/port/zephyr/plat.c
  ${PYMETAL_ROOT}/src/pymergetic/port/zephyr/kernel_static_port.c
  ${PYMETAL_ROOT}/src/pymergetic/port/zephyr/kernel_heap_port.c
  ${PYMETAL_ROOT}/src/pymergetic/port/zephyr/userspace_blob_port.c
)

set(PYMETAL_CORE_SOURCES
  ${PYMETAL_ROOT}/src/pymergetic/metal/memory/layout.c
  ${PYMETAL_ROOT}/src/pymergetic/metal/memory/arena.c
  ${PYMETAL_ROOT}/src/pymergetic/metal/memory/boot.c
  ${PYMETAL_ROOT}/src/pymergetic/metal/util/size.c
)

if(CONFIG_PM_MEMORY_BENCH)
  list(APPEND PYMETAL_CORE_SOURCES
    ${PYMETAL_ROOT}/src/pymergetic/metal/memory/bench.c
    ${PYMETAL_ROOT}/src/pymergetic/port/zephyr/bench_port.c
  )
endif()

if(CONFIG_PM_USERSPACE_BLOB)
  if(NOT EXISTS "${TLSF_DIR}/tlsf.h")
    message(FATAL_ERROR "tlsf not found at ${TLSF_DIR}; run west update from packages/metal")
  endif()

  list(APPEND PYMETAL_PORT_SOURCES
    ${PYMETAL_ROOT}/src/pymergetic/port/zephyr/userspace_blob.c
    ${PYMETAL_ROOT}/src/pymergetic/port/zephyr/mmap_port.c
    ${PYMETAL_ROOT}/src/pymergetic/port/zephyr/tlsf_malloc_port.c
    ${TLSF_DIR}/tlsf.c
  )

  if(CONFIG_PM_USERSPACE_BLOB_MEMTEST)
    list(APPEND PYMETAL_PORT_SOURCES
      ${PYMETAL_ROOT}/src/pymergetic/port/common/memtest.c
      ${PYMETAL_ROOT}/src/pymergetic/port/zephyr/memtest_parallel.c
    )
  endif()

  zephyr_include_directories(${TLSF_DIR})
  zephyr_compile_options(-fno-builtin-malloc)
  zephyr_ld_options(
    -Wl,--wrap=mmap
    -Wl,--wrap=munmap
  )
endif()

target_sources(app PRIVATE src/main.c ${PYMETAL_CORE_SOURCES} ${PYMETAL_PORT_SOURCES})

if(CONFIG_BUILD_OUTPUT_EFI)
  target_sources(app PRIVATE
    ${PYMETAL_ROOT}/src/pymergetic/port/zephyr/efi_ram.c
  )
  zephyr_include_directories(${PYMETAL_ZEPHYR_BASE}/arch/x86/zefi)
endif()
