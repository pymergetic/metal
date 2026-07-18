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

set(LZ4_ROOT_DIR "${PM_METAL_ROOT}/external/lz4")
set_source_files_properties(${PM_COMMON}/util/lz4.c PROPERTIES INCLUDE_DIRECTORIES ${PM_METAL_ROOT})

set(MICROTAR_ROOT_DIR "${PM_METAL_ROOT}/external/microtar")
set_source_files_properties(${PM_COMMON}/util/tar.c PROPERTIES INCLUDE_DIRECTORIES ${PM_METAL_ROOT})

set(MONOCYPHER_ROOT_DIR "${PM_METAL_ROOT}/external/monocypher")
set_source_files_properties(${PM_COMMON}/util/crypto.c PROPERTIES INCLUDE_DIRECTORIES ${PM_METAL_ROOT})

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
set(WAMR_BUILD_LIB_WASI_THREADS 1)
set(WAMR_BUILD_MULTI_MODULE 1)
set(WAMR_BUILD_GLOBAL_HEAP_POOL 0)

if(NOT EXISTS "${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake")
	message(FATAL_ERROR "WAMR not found at ${WAMR_ROOT_DIR}; run scripts/setup-west.sh")
endif()

if(CONFIG_64BIT)
	set(WAMR_BUILD_TARGET "X86_64")
else()
	set(WAMR_BUILD_TARGET "X86_32")
endif()

include(${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

set(PM_WAMR_LIBC_UTIL "${WAMR_ROOT_DIR}/core/shared/platform/common/libc-util")

# Embed verify mods when enabled
set(PM_MODS_EMBED_C "${CMAKE_CURRENT_LIST_DIR}/generated/mods_embed.c")
set(PM_MODS_EMBED_H "${CMAKE_CURRENT_LIST_DIR}/generated/mods_embed.h")
set(PM_POPULATE_EMBED_C "${CMAKE_CURRENT_LIST_DIR}/generated/populate_embed.c")
if(CONFIG_PM_METAL_VERIFY_MODS)
	if(NOT EXISTS "${PM_MODS_EMBED_C}")
		message(FATAL_ERROR
			"Missing ${PM_MODS_EMBED_C}; run scripts/gen-zephyr-mods-embed.sh")
	endif()
endif()

target_sources(app PRIVATE
	main.c
	${PM_COMMON}/port/platform.c
	${PM_COMMON}/memory/ops.c
	${PM_COMMON}/mount/ops.c
	${PM_COMMON}/mount/hostdir.c
	${PM_COMMON}/mount/proc.c
	${PM_COMMON}/mount/proc/util.c
	${PM_COMMON}/mount/proc/guest.c
	${PM_COMMON}/mount/proc/boot.c
	${PM_COMMON}/mount/proc/mounts.c
	${PM_COMMON}/mount/proc/filesystems.c
	${PM_COMMON}/mount/proc/version.c
	${PM_COMMON}/mount/proc/cpuinfo.c
	${PM_COMMON}/mount/proc/meminfo.c
	${PM_COMMON}/mount/proc/cmdline.c
	${PM_COMMON}/mount/proc/uptime.c
	${PM_COMMON}/mount/proc/environ.c
	${PM_COMMON}/mount/tmpfs_registry.c
	${PM_COMMON}/mount/populate.c
	${PM_COMMON}/mount/table.c
	${PM_COMMON}/mount/mount.c
	${PM_COMMON}/mount/fstab.c
	${PM_COMMON}/runtime/runtime.c
	${PM_COMMON}/runtime/process.c
	${PM_COMMON}/runtime/env.c
	${PM_COMMON}/app/app.c
	${PM_COMMON}/util/size.c
	${PM_COMMON}/util/arena.c
	${PM_COMMON}/util/endian.c
	${PM_COMMON}/util/fourcc.c
	${PM_COMMON}/util/eightcc.c
	${PM_COMMON}/util/log.c
	${PM_COMMON}/util/lz4.c
	${LZ4_ROOT_DIR}/lib/lz4.c
	${PM_COMMON}/util/tar.c
	${MICROTAR_ROOT_DIR}/src/microtar.c
	${PM_COMMON}/util/crypto.c
	${MONOCYPHER_ROOT_DIR}/src/monocypher.c
	${PM_COMMON}/net/ntp.c
	${PM_COMMON}/net/dns.c
	${PM_COMMON}/net/udp.c
	${PM_COMMON}/net/tcp.c
	${PM_COMMON}/net/tls.c
	${PM_COMMON}/net/http.c
	pymergetic/metal/port/platform.c
	pymergetic/metal/port/dns.c
	pymergetic/metal/port/tls.c
	pymergetic/metal/port/udp.c
	pymergetic/metal/port/tcp.c
	pymergetic/metal/port/lock.c
	pymergetic/metal/port/worker.c
	pymergetic/metal/port/pipe.c
	pymergetic/metal/memory/budget.c
	pymergetic/metal/memory/ram.c
	pymergetic/metal/memory/kheap.c
	pymergetic/metal/memory/bytecode.c
	pymergetic/metal/mount/device.c
	pymergetic/metal/mount/tmpfs.c
	pymergetic/metal/wasi/file.c
	pymergetic/metal/wasi/support.c
	pymergetic/metal/wasi/socket.c
	${WAMR_RUNTIME_LIB_SOURCE}
	${WAMR_ROOT_DIR}/core/shared/platform/common/libc-util/libc_errno.c
)

if(CONFIG_PM_METAL_VERIFY_MODS)
	target_sources(app PRIVATE ${PM_METAL_ROOT}/tests/zephyr_verify.c)
	if(EXISTS "${PM_MODS_EMBED_C}")
		target_sources(app PRIVATE ${PM_MODS_EMBED_C})
	endif()
	if(EXISTS "${PM_POPULATE_EMBED_C}")
		target_sources(app PRIVATE ${PM_POPULATE_EMBED_C})
	endif()
endif()

target_include_directories(app PRIVATE
	${PM_METAL_ROOT}/tests
	${PM_WAMR_LIBC_UTIL}
	${WAMR_ROOT_DIR}/core/shared/platform/zephyr
	${WAMR_ROOT_DIR}/core/shared/platform/include
	${WAMR_ROOT_DIR}/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/include
	${WAMR_ROOT_DIR}/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/src
)

target_compile_definitions(app PRIVATE
	PM_METAL_PORT_TARGET=PM_METAL_PORT_TARGET_ZEPHYR
	PM_METAL_RUNTIME_MULTI_MODULE=1
	PM_METAL_ZEPHYR_WASI_FILE=1
)

# posix.c / blocking_op.c need poll/htons/ioctl stubs. TARGET_DIRECTORY is
# required: these sources are added via WAMR's runtime_lib.cmake (another
# dir), and without it COMPILE_OPTIONS can attach to the wrong file entries
# or leak across the app target. Keep shim.h free of <fcntl.h> so a scoped
# -include cannot redefine Zephyr util.h's KB/MB/GB/MIN into other TUs.
set(PM_WASI_SSP_DIR
	"${WAMR_ROOT_DIR}/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/src")
set(PM_WASI_SHIM_INCLUDE
	"-include${CMAKE_CURRENT_LIST_DIR}/pymergetic/metal/wasi/shim.h")
set_source_files_properties(
	"${PM_WASI_SSP_DIR}/posix.c"
	"${PM_WASI_SSP_DIR}/blocking_op.c"
	TARGET_DIRECTORY app
	PROPERTIES COMPILE_OPTIONS "${PM_WASI_SHIM_INCLUDE}"
)

# Upstream WAMR noise (float32→double printf; unused loader locals) — not ours.
# Scoped to WAMR TUs only so metal sources still see those warnings.
foreach(_pm_wamr_src IN LISTS WAMR_RUNTIME_LIB_SOURCE)
	set_property(SOURCE "${_pm_wamr_src}" TARGET_DIRECTORY app APPEND PROPERTY
		COMPILE_OPTIONS -Wno-double-promotion -Wno-unused-variable)
endforeach()
set_property(SOURCE
	"${WAMR_ROOT_DIR}/core/shared/platform/common/libc-util/libc_errno.c"
	TARGET_DIRECTORY app APPEND PROPERTY
	COMPILE_OPTIONS -Wno-double-promotion -Wno-unused-variable)

target_compile_options(app PRIVATE
	-Wno-type-limits
)

# WAMR invokeNative_*.s omit .note.GNU-stack; force a non-executable stack on
# the final ELF without patching external/wamr (keeps the tree vanilla).
target_link_options(app PRIVATE -Wl,-z,noexecstack)

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
