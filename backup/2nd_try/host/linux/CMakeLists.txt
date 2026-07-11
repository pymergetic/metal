cmake_minimum_required(VERSION 3.20)
project(pymergetic-linux C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PM_ROOT "${CMAKE_SOURCE_DIR}/../..")
set(PM_INCLUDE "${PM_ROOT}/include")
set(PM_SRC "${PM_ROOT}/src")

set(PM_UPPER_COMMON
	${PM_SRC}/pymergetic/metal/sys/sys.c
	${PM_SRC}/pymergetic/metal/orchestrator/boot.c
)

set(PM_ENGINE_COMMON
	${PM_ROOT}/host/common/pymergetic/metal/runtime/entry.c
	${PM_ROOT}/host/common/pymergetic/metal/util/bigtag.c
	${PM_ROOT}/host/common/pymergetic/metal/util/eightcc.c
	${PM_ROOT}/host/common/pymergetic/metal/util/endian.c
	${PM_ROOT}/host/common/pymergetic/metal/util/fourcc.c
	${PM_ROOT}/host/common/pymergetic/metal/util/version.c
	${PM_ROOT}/host/common/pymergetic/metal/util/versiontag.c
	${PM_ROOT}/host/common/pymergetic/metal/util/wiretag.c
)

set(PM_ENGINE_LINUX
	${PM_ENGINE_COMMON}
	${PM_UPPER_COMMON}
	${PM_ROOT}/host/linux/main.c
	${PM_ROOT}/host/linux/pymergetic/metal/sys/hostinfo.c
	${PM_ROOT}/host/linux/pymergetic/metal/sys/sys.c
	${PM_ROOT}/host/linux/pymergetic/metal/port/plat.c
)

add_executable(pm-linux-engine ${PM_ENGINE_LINUX})
target_include_directories(pm-linux-engine PRIVATE ${PM_INCLUDE})
target_compile_definitions(pm-linux-engine PRIVATE PM_METAL_BUILD_KERNEL=1)
