# SPDX-License-Identifier: Apache-2.0
# Freestanding EFI app scaffold. Full PE/COFF + gnu-efi/clang link TBD.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	cmake_minimum_required(VERSION 3.20)
	project(pm_metal_efi C)
endif()

add_library(pm_metal_efi_scaffold OBJECT main.c)
target_compile_options(pm_metal_efi_scaffold PRIVATE -ffreestanding -fno-stack-protector)
