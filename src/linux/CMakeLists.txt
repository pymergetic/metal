# SPDX-License-Identifier: Apache-2.0
# Linux pm-metal runtime — scaffold. See docs/SOURCETREE.md.

cmake_minimum_required(VERSION 3.20)
project(pm-linux-runtime C)

get_filename_component(PM_METAL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(PM_COMMON "${PM_METAL_ROOT}/src/common/pymergetic/metal")
set(PM_COMMON_INC "${PM_METAL_ROOT}/src/common")

add_executable(pm-linux-runtime
	main.c
	pymergetic/metal/port/platform.c
	${PM_COMMON}/port/platform.c
	${PM_COMMON}/runtime/runtime.c
)

target_include_directories(pm-linux-runtime PRIVATE
	${PM_COMMON_INC}
)

target_compile_definitions(pm-linux-runtime PRIVATE
	PM_METAL_PORT_TARGET=PM_METAL_PORT_TARGET_LINUX
)

# WAMR + link flags wired in scripts/build-linux.sh when implemented.
