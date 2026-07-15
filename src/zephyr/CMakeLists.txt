# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

if(DEFINED ENV{ZEPHYR_BASE} AND NOT "$ENV{ZEPHYR_BASE}" STREQUAL "")
	set(PM_ZEPHYR_BASE $ENV{ZEPHYR_BASE})
else()
	get_filename_component(PM_METAL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
	set(PM_ZEPHYR_BASE "${PM_METAL_ROOT}/external/zephyr")
endif()

if(NOT EXISTS "${PM_ZEPHYR_BASE}/share/zephyr-package/cmake/ZephyrConfig.cmake")
	message(FATAL_ERROR "Zephyr not found at ${PM_ZEPHYR_BASE}; run scripts/setup-west.sh")
endif()

find_package(Zephyr REQUIRED HINTS ${PM_ZEPHYR_BASE})
project(pm-zephyr-runtime)

get_filename_component(PM_METAL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(PM_COMMON "${PM_METAL_ROOT}/src/common/pymergetic/metal")
set(PM_COMMON_INC "${PM_METAL_ROOT}/src/common")

# ---- LZ4 (vendored, vanilla — see docs/SOURCETREE.md; block API only).
# util/lz4.c includes it as "external/lz4/lib/lz4.h" — a full path from
# PM_METAL_ROOT, not a bare "lz4.h" — but PM_METAL_ROOT is *not* added
# via zephyr_include_directories() for that: that macro feeds the global
# zephyr_interface, i.e. every Zephyr subsystem in the build, not just
# this app — scoped to just this one source file instead, see below. ----
set(LZ4_ROOT_DIR "${PM_METAL_ROOT}/external/lz4")
set_source_files_properties(${PM_COMMON}/util/lz4.c PROPERTIES INCLUDE_DIRECTORIES ${PM_METAL_ROOT})

# ---- microtar (vendored + patched — see docs/SOURCETREE.md; same
# "scoped include dir, not zephyr_include_directories()" shape as LZ4
# above) ----
set(MICROTAR_ROOT_DIR "${PM_METAL_ROOT}/external/microtar")
set_source_files_properties(${PM_COMMON}/util/tar.c PROPERTIES INCLUDE_DIRECTORIES ${PM_METAL_ROOT})

zephyr_include_directories(${PM_COMMON_INC})
zephyr_include_directories(${PM_METAL_ROOT}/include)
zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR})

if(DEFINED ENV{WAMR_ROOT_DIR} AND NOT "$ENV{WAMR_ROOT_DIR}" STREQUAL "")
	set(WAMR_ROOT_DIR $ENV{WAMR_ROOT_DIR})
else()
	set(WAMR_ROOT_DIR "${PM_METAL_ROOT}/external/wamr")
endif()

set(WAMR_BUILD_PLATFORM "zephyr")
set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_AOT 0)
set(WAMR_BUILD_LIBC_BUILTIN 1)
set(WAMR_BUILD_LIBC_WASI 1)
set(WAMR_BUILD_LIB_PTHREAD 0)
set(WAMR_BUILD_GLOBAL_HEAP_POOL 1)
set(WAMR_BUILD_GLOBAL_HEAP_SIZE 524288)

if(NOT EXISTS "${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake")
	message(FATAL_ERROR "WAMR not found at ${WAMR_ROOT_DIR}; run scripts/setup-west.sh")
endif()

if(CONFIG_64BIT)
	set(WAMR_BUILD_TARGET "X86_64")
else()
	set(WAMR_BUILD_TARGET "X86_32")
endif()

include(${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

target_sources(app PRIVATE
	main.c
	${PM_COMMON}/port/platform.c
	${PM_COMMON}/memory/ops.c
	${PM_COMMON}/runtime/runtime.c
	${PM_COMMON}/runtime/process.c
	${PM_COMMON}/app/app.c
	${PM_COMMON}/util/size.c
	${PM_COMMON}/util/arena.c
	${PM_COMMON}/util/log.c
	${PM_COMMON}/util/lz4.c
	${LZ4_ROOT_DIR}/lib/lz4.c
	${PM_COMMON}/util/tar.c
	${MICROTAR_ROOT_DIR}/src/microtar.c
	pymergetic/metal/port/platform.c
	pymergetic/metal/port/lock.c
	pymergetic/metal/port/worker.c
	pymergetic/metal/memory/ram.c
	pymergetic/metal/memory/kheap.c
	pymergetic/metal/memory/bytecode.c
	pymergetic/metal/wasi/file.c
	${WAMR_RUNTIME_LIB_SOURCE}
)

target_compile_definitions(app PRIVATE PM_METAL_PORT_TARGET=PM_METAL_PORT_TARGET_ZEPHYR)
