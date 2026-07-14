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
set(PM_SHARED "${PM_METAL_ROOT}/src/shared/pymergetic/metal")

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
	${PM_COMMON}/console/viewport.c
	${PM_COMMON}/runtime/runtime.c
	${PM_COMMON}/runtime/process.c
	${PM_COMMON}/app/app.c
	${PM_COMMON}/shell/shell.c
	${PM_COMMON}/shell/handles.c
	${PM_COMMON}/shell/guest_exec.c
	${PM_COMMON}/shell/commands.c
	${PM_COMMON}/shell/commands/help.c
	${PM_COMMON}/shell/commands/load.c
	${PM_COMMON}/shell/commands/run.c
	${PM_COMMON}/shell/commands/unload.c
	${PM_COMMON}/shell/commands/focus.c
	${PM_COMMON}/shell/commands/ps.c
	${PM_COMMON}/shell/commands/cd.c
	${PM_COMMON}/shell/commands/pwd.c
	${PM_COMMON}/shell/commands/ls.c
	${PM_COMMON}/shell/commands/export.c
	${PM_COMMON}/shell/commands/env.c
	${PM_COMMON}/shell/commands/quit.c
	${PM_COMMON}/shell/commands/exit.c
	${PM_COMMON}/shell/commands/uname.c
	${PM_COMMON}/shell/commands/sleep.c
	${PM_SHARED}/util/size.c
	${PM_SHARED}/util/arena.c
	${PM_SHARED}/util/log.c
	pymergetic/metal/port/platform.c
	pymergetic/metal/port/lock.c
	pymergetic/metal/port/worker.c
	pymergetic/metal/port/term.c
	pymergetic/metal/port/dir.c
	pymergetic/metal/port/intr.c
	pymergetic/metal/port/sleep.c
	pymergetic/metal/memory/ram.c
	pymergetic/metal/memory/kheap.c
	pymergetic/metal/memory/bytecode.c
	pymergetic/metal/console/console.c
	pymergetic/metal/console/viewport.c
	pymergetic/metal/wasi/file.c
	${WAMR_RUNTIME_LIB_SOURCE}
)

target_compile_definitions(app PRIVATE PM_METAL_PORT_TARGET=PM_METAL_PORT_TARGET_ZEPHYR)
