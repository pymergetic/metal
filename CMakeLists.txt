# SPDX-License-Identifier: Apache-2.0
# Umbrella — freestanding EFI Metal (hosted ports archived).
# See docs/SOURCETREE.md, src/efi/README.md.

cmake_minimum_required(VERSION 3.20)
project(pymergetic-metal C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_subdirectory(src/efi)
