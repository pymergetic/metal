cmake_minimum_required(VERSION 3.20)
project(pymergetic-slice C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PM_ROOT "${CMAKE_SOURCE_DIR}/../..")
set(PM_INCLUDE "${PM_ROOT}/include")

set(PM_ENGINE_COMMON
	${PM_ROOT}/host/common/pymergetic/metal/util/endian.c
	${PM_ROOT}/host/common/pymergetic/metal/util/fourcc.c
	${PM_ROOT}/host/common/pymergetic/metal/util/version.c
	${PM_ROOT}/host/common/pymergetic/metal/util/wiretag.c
)

set(PM_ENGINE_LINUX
	${PM_ENGINE_COMMON}
	${PM_ROOT}/host/linux/main.c
	${PM_ROOT}/host/linux/pymergetic/metal/sys/hostinfo.c
	${PM_ROOT}/host/linux/pymergetic/metal/sys/sys.c
	${PM_ROOT}/host/linux/pymergetic/metal/port/plat.c
)

set(PM_ENGINE_ZEPHYR
	${PM_ENGINE_COMMON}
	${PM_ROOT}/host/zephyr/engine_run.c
	${PM_ROOT}/host/zephyr/pymergetic/metal/sys/hostinfo.c
	${PM_ROOT}/host/zephyr/pymergetic/metal/sys/sys.c
	${PM_ROOT}/host/zephyr/pymergetic/metal/port/plat.c
)

add_executable(pm-linux-engine ${PM_ENGINE_LINUX})
target_include_directories(pm-linux-engine PRIVATE ${PM_INCLUDE})

add_executable(pm-zephyr-engine ${PM_ENGINE_ZEPHYR})
target_include_directories(pm-zephyr-engine PRIVATE ${PM_INCLUDE})
