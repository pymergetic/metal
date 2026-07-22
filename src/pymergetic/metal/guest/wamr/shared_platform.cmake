# Freestanding EFI WAMR platform (documentation / cmake dumps).
# Not wired into EDK2 INF builds directly — MetalPkg lists sources instead.
#
# Expected include paths from the consumer:
#   - this directory (platform_internal.h)
#   - external/wamr/core/shared/platform/include
#   - include/pymergetic/metal (and src/pymergetic/metal for mem/time)

set(PLATFORM_SHARED_DIR ${CMAKE_CURRENT_LIST_DIR})

add_definitions(-DBH_PLATFORM_METAL_EFI)

include_directories(${PLATFORM_SHARED_DIR})

# Resolve WAMR root: src/efi/pymergetic/metal/wamr -> package root -> external/wamr
if(NOT DEFINED WAMR_ROOT_DIR)
  get_filename_component(_PM_METAL_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
  set(WAMR_ROOT_DIR "${_PM_METAL_ROOT}/external/wamr")
endif()

include_directories(${WAMR_ROOT_DIR}/core/shared/platform/include)

include(
  ${WAMR_ROOT_DIR}/core/shared/platform/common/math/platform_api_math.cmake)

set(PLATFORM_SHARED_SOURCE
  ${PLATFORM_SHARED_DIR}/efi_platform.c
  ${PLATFORM_SHARED_DIR}/efi_thread.c
  ${PLATFORM_SHARED_DIR}/efi_socket.c
  ${PLATFORM_SHARED_DIR}/efi_wasi_fs.c
  ${PLATFORM_COMMON_MATH_SOURCE}
)
