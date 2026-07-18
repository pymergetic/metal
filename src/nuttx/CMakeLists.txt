# ##############################################################################
# apps/system/pm_metal/CMakeLists.txt (symlinked from packages/metal/src/nuttx)
#
# SPDX-License-Identifier: Apache-2.0
# NuttX pm-metal runtime — see src/nuttx/README.md
# ##############################################################################

if(CONFIG_PYMERGETIC_METAL)

  get_filename_component(PM_METAL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
  # Symlink apps/system/pm_metal -> src/nuttx: resolve to packages/metal.
  get_filename_component(PM_METAL_ROOT "${CMAKE_CURRENT_LIST_DIR}" REALPATH)
  get_filename_component(PM_METAL_ROOT "${PM_METAL_ROOT}/../.." ABSOLUTE)

  set(PM_COMMON "${PM_METAL_ROOT}/src/common/pymergetic/metal")
  set(PM_COMMON_INC "${PM_METAL_ROOT}/src/common")
  set(PM_INCLUDE_INC "${PM_METAL_ROOT}/include")

  if(DEFINED ENV{WAMR_ROOT_DIR} AND NOT "$ENV{WAMR_ROOT_DIR}" STREQUAL "")
    set(WAMR_ROOT_DIR $ENV{WAMR_ROOT_DIR})
  else()
    set(WAMR_ROOT_DIR "${PM_METAL_ROOT}/external/wamr")
  endif()

  if(NOT EXISTS "${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake")
    message(FATAL_ERROR "WAMR not found at ${WAMR_ROOT_DIR}; run scripts/setup wamr")
  endif()

  set(WAMR_BUILD_PLATFORM "nuttx")
  set(WAMR_BUILD_INTERP 1)
  set(WAMR_BUILD_FAST_INTERP 1)
  set(WAMR_BUILD_AOT 0)
  set(WAMR_BUILD_JIT 0)
  set(WAMR_BUILD_FAST_JIT 0)
  set(WAMR_BUILD_LIBC_BUILTIN 1)
  set(WAMR_BUILD_LIBC_WASI 1)
  set(WAMR_BUILD_MULTI_MODULE 1)
  set(WAMR_BUILD_LIB_PTHREAD 0)
  set(WAMR_BUILD_LIB_WASI_THREADS 1)
  set(WAMR_BUILD_MINI_LOADER 0)
  set(WAMR_BUILD_SIMD 0)
  set(WAMR_BUILD_REF_TYPES 1)
  set(WAMR_BUILD_DEBUG_INTERP 0)

  if(CONFIG_ARCH_SIM)
    if(CONFIG_SIM_M32 OR CONFIG_HOST_X86)
      set(WAMR_BUILD_TARGET "X86_32")
    elseif(CONFIG_HOST_ARM)
      set(WAMR_BUILD_TARGET "ARM")
    elseif(CONFIG_HOST_ARM64)
      set(WAMR_BUILD_TARGET "AARCH64")
    else()
      set(WAMR_BUILD_TARGET "X86_64")
    endif()
  elseif(CONFIG_ARCH_X86_64)
    set(WAMR_BUILD_TARGET "X86_64")
  elseif(CONFIG_ARCH_X86)
    set(WAMR_BUILD_TARGET "X86_32")
  elseif(CONFIG_ARCH_ARM64)
    set(WAMR_BUILD_TARGET "AARCH64")
  elseif(CONFIG_ARCH_RV64)
    set(WAMR_BUILD_TARGET "RISCV64")
  elseif(CONFIG_ARCH_RV32)
    set(WAMR_BUILD_TARGET "RISCV32")
  else()
    set(WAMR_BUILD_TARGET "X86_64")
  endif()

  include(${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

  # Drop sources that must not build into the NuttX Metal app:
  # - posix_file.c: Metal wasi/file.c + posix_file_real.c provide os_* instead
  # - posix_memmap.c: NuttX uses nuttx_platform.c's os_mmap; upstream
  #   shared_platform.cmake's REMOVE_ITEM is broken (${list} vs list name),
  #   so posix_memmap.c still lands in WAMR_RUNTIME_LIB_SOURCE and fails
  #   (MAP_32BIT / mremap) on this libc.
  #
  # Host cmake check_symbol_exists(mremap) is true on the Linux *build*
  # host, so runtime_lib.cmake skips common/memory/mremap.c — but the
  # NuttX target has no mremap. After dropping posix_memmap.c we must
  # supply the slow os_mremap ourselves.
  set(_pm_wamr_filtered "")
  foreach(_pm_src IN LISTS WAMR_RUNTIME_LIB_SOURCE)
    get_filename_component(_pm_base "${_pm_src}" NAME)
    if(_pm_base STREQUAL "posix_file.c" OR _pm_base STREQUAL "posix_memmap.c")
      continue()
    endif()
    list(APPEND _pm_wamr_filtered "${_pm_src}")
  endforeach()
  list(APPEND _pm_wamr_filtered
       "${WAMR_ROOT_DIR}/core/shared/platform/common/memory/mremap.c")
  set(WAMR_RUNTIME_LIB_SOURCE ${_pm_wamr_filtered})

  set(LZ4_ROOT_DIR "${PM_METAL_ROOT}/external/lz4")
  set_source_files_properties(${PM_COMMON}/util/lz4.c
                              PROPERTIES INCLUDE_DIRECTORIES ${PM_METAL_ROOT})

  set(MICROTAR_ROOT_DIR "${PM_METAL_ROOT}/external/microtar")
  set_source_files_properties(${PM_COMMON}/util/tar.c
                              PROPERTIES INCLUDE_DIRECTORIES ${PM_METAL_ROOT})

  set(MONOCYPHER_ROOT_DIR "${PM_METAL_ROOT}/external/monocypher")
  set_source_files_properties(${PM_COMMON}/util/crypto.c
                              PROPERTIES INCLUDE_DIRECTORIES ${PM_METAL_ROOT})

  set(PM_METAL_NUTTX_SRCS
      main.c
      pymergetic/metal/port/platform.c
      pymergetic/metal/port/dns.c
      pymergetic/metal/port/tls.c
      pymergetic/metal/port/udp.c
      pymergetic/metal/port/tcp.c
      pymergetic/metal/port/lock.c
      pymergetic/metal/port/worker.c
      pymergetic/metal/port/pipe.c
      pymergetic/metal/memory/ram.c
      pymergetic/metal/memory/kheap.c
      pymergetic/metal/memory/bytecode.c
      pymergetic/metal/mount/tmpfs.c
      pymergetic/metal/wasi/file.c
      pymergetic/metal/wasi/posix_file_real.c
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
      ${PM_COMMON}/mount/pkg.c
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
      ${WAMR_RUNTIME_LIB_SOURCE})

  # Named lz4 guest packages (scripts/lib/guest-pkgs.sh → build/guest-pkgs/).
  set(PM_METAL_GUEST_PKGS_CMAKE "${PM_METAL_ROOT}/build/guest-pkgs/pkgs.cmake")
  if(EXISTS "${PM_METAL_GUEST_PKGS_CMAKE}")
    include(${PM_METAL_GUEST_PKGS_CMAKE})
    if(DEFINED PM_METAL_GUEST_PKG_SOURCES)
      list(APPEND PM_METAL_NUTTX_SRCS ${PM_METAL_GUEST_PKG_SOURCES})
    else()
      list(APPEND PM_METAL_NUTTX_SRCS ${PM_COMMON}/mount/pkg_embed_stub.c)
    endif()
  else()
    list(APPEND PM_METAL_NUTTX_SRCS ${PM_COMMON}/mount/pkg_embed_stub.c)
  endif()

  set(PM_METAL_NUTTX_DEFS
      PM_METAL_PORT_TARGET=PM_METAL_PORT_TARGET_NUTTX
      PM_METAL_RUNTIME_MULTI_MODULE=1
      WASM_HAVE_MREMAP=0
      PM_METAL_HAVE_CURL=1)
  # Host mmap pools for large python.wasm budgets — see memory/{kheap,bytecode}.c
  if(CONFIG_ARCH_SIM)
    list(APPEND PM_METAL_NUTTX_DEFS PM_METAL_NUTTX_HOST_POOL=1)
  endif()

  nuttx_add_application(
    NAME
    pm_metal
    MODULE
    ${CONFIG_PYMERGETIC_METAL}
    PRIORITY
    ${CONFIG_PYMERGETIC_METAL_PRIORITY}
    STACKSIZE
    ${CONFIG_PYMERGETIC_METAL_STACKSIZE}
    SRCS
    ${PM_METAL_NUTTX_SRCS}
    INCLUDE_DIRECTORIES
    ${PM_COMMON_INC}
    ${PM_INCLUDE_INC}
    ${PM_METAL_ROOT}/external/curl/include
    ${WAMR_ROOT_DIR}/core/shared/platform/nuttx
    ${WAMR_ROOT_DIR}/core/shared/platform/include
    ${WAMR_ROOT_DIR}/core/shared/platform/common/posix
    ${WAMR_ROOT_DIR}/core/shared/platform/common/libc-util
    ${WAMR_ROOT_DIR}/core/iwasm/include
    ${WAMR_ROOT_DIR}/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/include
    ${WAMR_ROOT_DIR}/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/src
    COMPILE_FLAGS
    -Wno-double-promotion
    -Wno-unused-variable
    -DWASM_HAVE_MREMAP=0
    DEFINITIONS
    ${PM_METAL_NUTTX_DEFS})

  # Host DNS/UDP + libcurl + large mmap pools for sim — final nuttx link
  # only (not nuttx.rel).
  if(CONFIG_ARCH_SIM)
    target_sources(nuttx PRIVATE
      ${CMAKE_CURRENT_LIST_DIR}/pymergetic/metal/port/host_net_adapt.c
      ${CMAKE_CURRENT_LIST_DIR}/pymergetic/metal/port/host_mem_adapt.c)
    target_include_directories(nuttx PRIVATE ${PM_INCLUDE_INC})
    target_link_libraries(nuttx PRIVATE
      dl
      /usr/lib/x86_64-linux-gnu/libcurl.so.4)
  endif()

endif()
