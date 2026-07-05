# SPDX-License-Identifier: Apache-2.0

if(CONFIG_PM_METAL)
zephyr_include_directories(include)
zephyr_library()
zephyr_library_sources(src/hello.c)
endif()
