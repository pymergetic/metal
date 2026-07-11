# SPDX-License-Identifier: Apache-2.0
# Umbrella build — IDE compile_commands + linux runtime.
# See docs/SOURCETREE.md.

cmake_minimum_required(VERSION 3.20)
project(pymergetic-metal C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(src/linux)
