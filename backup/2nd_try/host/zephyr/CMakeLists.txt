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
set(PM_INCLUDE "${PM_METAL_ROOT}/include")
set(PM_SRC "${PM_METAL_ROOT}/src")
set(PM_COMMON_UTIL "${PM_METAL_ROOT}/host/common/pymergetic/metal/util")
set(PM_COMMON_RUNTIME "${PM_METAL_ROOT}/host/common/pymergetic/metal/runtime")
set(PM_UPPER
	${PM_SRC}/pymergetic/metal/sys/sys.c
	${PM_SRC}/pymergetic/metal/orchestrator/boot.c
)

if(DEFINED ENV{WAMR_ROOT_DIR} AND NOT "$ENV{WAMR_ROOT_DIR}" STREQUAL "")
	set(WAMR_ROOT_DIR $ENV{WAMR_ROOT_DIR})
else()
	set(WAMR_ROOT_DIR "${PM_METAL_ROOT}/external/wamr")
endif()

set(PM_WAMR_LIBC_UTIL "${WAMR_ROOT_DIR}/core/shared/platform/common/libc-util")

zephyr_include_directories(${PM_INCLUDE})
zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR})
zephyr_include_directories("${PM_WAMR_LIBC_UTIL}")

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

set(PM_RUNTIME_SOURCES
	src/main.c
	${PM_COMMON_RUNTIME}/entry.c
	${PM_UPPER}
	pymergetic/metal/port/plat.c
	pymergetic/metal/sys/sys.c
	pymergetic/metal/sys/hostinfo.c
	pymergetic/metal/orchestrator/wasm_host.c
	pymergetic/wasi/zephyr_file.c
	pymergetic/wasi/zephyr_wasi_support.c
	${WAMR_RUNTIME_LIB_SOURCE}
	${WAMR_ROOT_DIR}/core/shared/platform/common/libc-util/libc_errno.c
	${PM_COMMON_UTIL}/bigtag.c
	${PM_COMMON_UTIL}/eightcc.c
	${PM_COMMON_UTIL}/endian.c
	${PM_COMMON_UTIL}/fourcc.c
	${PM_COMMON_UTIL}/version.c
	${PM_COMMON_UTIL}/versiontag.c
	${PM_COMMON_UTIL}/wiretag.c
)

if(CONFIG_PM_METAL_VERIFY_MOD_SMOKE)
	set(PM_MOD_SMOKE_EMBED "${CMAKE_CURRENT_LIST_DIR}/generated/mod_smoke_wasm.c")
	if(NOT EXISTS "${PM_MOD_SMOKE_EMBED}")
		message(FATAL_ERROR "Missing ${PM_MOD_SMOKE_EMBED}; run scripts/gen-mod-smoke-embed.sh")
	endif()
	list(APPEND PM_RUNTIME_SOURCES
		src/verify_mod_smoke.c
		${PM_MOD_SMOKE_EMBED}
	)
endif()

target_sources(app PRIVATE ${PM_RUNTIME_SOURCES})
target_compile_options(app PRIVATE -Wno-type-limits)
string(TIMESTAMP PM_METAL_BUILT_EPOCH "%s" UTC)
target_compile_definitions(app PRIVATE
	PM_METAL_BUILD_KERNEL=1
	PM_METAL_BUILT_EPOCH=${PM_METAL_BUILT_EPOCH}
	PM_METAL_ZEPHYR_WASI_FILE=1
)
target_compile_options(app PRIVATE
	"$<$<COMPILE_LANGUAGE:C>:-include${CMAKE_CURRENT_LIST_DIR}/pymergetic/wasi/zephyr_shim.h>"
)
target_include_directories(app PRIVATE "${PM_WAMR_LIBC_UTIL}")

if(DEFINED ENV{WASI_SDK_PATH} AND NOT "$ENV{WASI_SDK_PATH}" STREQUAL "")
	set(WASI_SDK_PATH $ENV{WASI_SDK_PATH})
elseif(EXISTS "${PM_METAL_ROOT}/.tools/wasi-sdk/share/wasi-sysroot/lib/wasm32-wasi/libm.a")
	set(WASI_SDK_PATH "${PM_METAL_ROOT}/.tools/wasi-sdk")
endif()

if(WASI_SDK_PATH)
	target_link_libraries(app PUBLIC
		"${WASI_SDK_PATH}/share/wasi-sysroot/lib/wasm32-wasi/libm.a"
		"${WASI_SDK_PATH}/share/wasi-sysroot/lib/wasm32-wasi/libdl.a")
endif()
