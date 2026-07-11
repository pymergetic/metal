# SPDX-License-Identifier: Apache-2.0
# Linux pm-metal runtime. See docs/SOURCETREE.md, docs/RUNTIME.md.

cmake_minimum_required(VERSION 3.20)
project(pm-linux-runtime C)

if (NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif ()

get_filename_component(PM_METAL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(PM_COMMON "${PM_METAL_ROOT}/src/common/pymergetic/metal")
set(PM_COMMON_INC "${PM_METAL_ROOT}/src/common")
set(PM_SHARED "${PM_METAL_ROOT}/src/shared/pymergetic/metal")
set(PM_INCLUDE_INC "${PM_METAL_ROOT}/include")

# ---- WAMR (vendored, vanilla — see docs/SOURCETREE.md) ----

set(WAMR_ROOT_DIR "${PM_METAL_ROOT}/external/wamr")
set(WAMR_BUILD_PLATFORM "linux")

if (NOT DEFINED WAMR_BUILD_TARGET)
	if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)")
		set(WAMR_BUILD_TARGET "AARCH64")
	elseif (CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(WAMR_BUILD_TARGET "X86_64")
	else ()
		set(WAMR_BUILD_TARGET "X86_32")
	endif ()
endif ()

set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_FAST_INTERP 1)
set(WAMR_BUILD_AOT 1)
set(WAMR_BUILD_JIT 0)
set(WAMR_BUILD_FAST_JIT 0)
set(WAMR_BUILD_LIBC_BUILTIN 1)
set(WAMR_BUILD_LIBC_WASI 1)
set(WAMR_BUILD_MULTI_MODULE 0)
set(WAMR_BUILD_LIB_PTHREAD 0)
set(WAMR_BUILD_LIB_WASI_THREADS 0)
set(WAMR_BUILD_MINI_LOADER 0)
set(WAMR_BUILD_SIMD 1)
set(WAMR_BUILD_REF_TYPES 1)
set(WAMR_BUILD_DEBUG_INTERP 0)

include(${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

add_library(pm-wamr-vmlib STATIC ${WAMR_RUNTIME_LIB_SOURCE})
target_include_directories(pm-wamr-vmlib PUBLIC ${WAMR_ROOT_DIR}/core/iwasm/include)
set_target_properties(pm-wamr-vmlib PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---- pm-linux-runtime ----

add_executable(pm-linux-runtime
	main.c
	pymergetic/metal/port/platform.c
	${PM_COMMON}/port/platform.c
	${PM_COMMON}/runtime/runtime.c
	${PM_SHARED}/util/size.c
)

target_include_directories(pm-linux-runtime PRIVATE
	${PM_COMMON_INC}
	${PM_INCLUDE_INC}
)

target_compile_definitions(pm-linux-runtime PRIVATE
	PM_METAL_PORT_TARGET=PM_METAL_PORT_TARGET_LINUX
)

target_link_libraries(pm-linux-runtime PRIVATE
	pm-wamr-vmlib
	m
	dl
	pthread
)
