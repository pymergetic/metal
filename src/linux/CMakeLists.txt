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
# Lets one .wasm import functions/globals/memory directly from another
# already-loaded .wasm instead of everything being statically linked into
# one file — see runtime/runtime.c's own module_reader (wired against this
# same vfs_root) and docs/RUNTIME.md "Multi-module" for the worked example
# and what WAMR does/doesn't actually share across importers.
set(WAMR_BUILD_MULTI_MODULE 1)
set(WAMR_BUILD_LIB_PTHREAD 0)
# Standards-track wasi-threads (thread-spawn import + shared linear memory),
# not WAMR's own older non-standard lib-pthread — this is also what turns on
# WASM_ENABLE_THREAD_MGR internally (see external/wamr/build-scripts/
# runtime_lib.cmake), which is what makes runtime.c's
# pm_metal_runtime_terminate() able to actually interrupt a hot compute loop
# (its CHECK_SUSPEND_FLAGS() poll is compiled out entirely without this —
# see scripts/verify linux none process) rather than only ever being noticed at
# an import-call boundary.
set(WAMR_BUILD_LIB_WASI_THREADS 1)
set(WAMR_BUILD_MINI_LOADER 0)
set(WAMR_BUILD_SIMD 1)
set(WAMR_BUILD_REF_TYPES 1)
set(WAMR_BUILD_DEBUG_INTERP 0)

include(${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

# Drop stock posix_file — Metal provides os_* (virtual proc + __real_* posix).
# --wrap cannot redirect calls inside libpm-wamr-vmlib.a.
list(REMOVE_ITEM WAMR_RUNTIME_LIB_SOURCE
	"${WAMR_ROOT_DIR}/core/shared/platform/common/posix/posix_file.c")

add_library(pm-wamr-vmlib STATIC ${WAMR_RUNTIME_LIB_SOURCE})
target_include_directories(pm-wamr-vmlib PUBLIC ${WAMR_ROOT_DIR}/core/iwasm/include)
set_target_properties(pm-wamr-vmlib PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Upstream WAMR noise (float32→double printf; unused loader locals) — not ours.
target_compile_options(pm-wamr-vmlib PRIVATE
	-Wno-double-promotion
	-Wno-unused-variable
)

# Virtual /proc os_* + renamed stock posix_file as __real_os_*.
# See docs/MOUNT.md Phase 6b.
set(PM_METAL_WASI_FILE_SRC
	pymergetic/metal/wasi/file.c
	pymergetic/metal/wasi/posix_file_real.c
)
set(PM_METAL_WASI_FILE_INCLUDES
	${WAMR_ROOT_DIR}/core/shared/platform/linux
	${WAMR_ROOT_DIR}/core/shared/platform/include
	${WAMR_ROOT_DIR}/core/shared/platform/common/posix
	${WAMR_ROOT_DIR}/core/shared/platform/common/libc-util
	${WAMR_ROOT_DIR}/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/include
	${WAMR_ROOT_DIR}/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/src
)

# ---- LZ4 (vendored, vanilla — see docs/SOURCETREE.md; block API only,
# util/lz4.c's own host impl links this straight in like any other
# common source, no separate static-lib step). util/lz4.c itself includes
# it as "external/lz4/lib/lz4.h" — a full path from PM_METAL_ROOT, not a
# bare "lz4.h" — but PM_METAL_ROOT is *not* added to any target's own
# include dirs for that (every other source in the target would also
# gain the whole repo tree as includable, for no reason of its own).
# Scoped to just this one source file instead — see below. ----

set(LZ4_ROOT_DIR "${PM_METAL_ROOT}/external/lz4")
set_source_files_properties(${PM_COMMON}/util/lz4.c PROPERTIES INCLUDE_DIRECTORIES ${PM_METAL_ROOT})

# ---- microtar (vendored + patched — see docs/SOURCETREE.md; same
# "scoped include dir, not a target-wide one" shape as LZ4 above) ----

set(MICROTAR_ROOT_DIR "${PM_METAL_ROOT}/external/microtar")
set_source_files_properties(${PM_COMMON}/util/tar.c PROPERTIES INCLUDE_DIRECTORIES ${PM_METAL_ROOT})

# ---- Monocypher (vendored — util/crypto.c; scoped include like LZ4) ----

set(MONOCYPHER_ROOT_DIR "${PM_METAL_ROOT}/external/monocypher")
set_source_files_properties(${PM_COMMON}/util/crypto.c PROPERTIES INCLUDE_DIRECTORIES ${PM_METAL_ROOT})

# ---- mbedTLS + nghttp2 + libcurl (HTTP/1.1 + HTTP/2) ----

include(${CMAKE_CURRENT_LIST_DIR}/cmake/PmNetDeps.cmake)

# ---- pm-linux-runtime ----

add_executable(pm-linux-runtime
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
	${PM_METAL_WASI_FILE_SRC}
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
)

# Named lz4 guest packages (scripts/lib/guest-pkgs.sh → build/guest-pkgs/).
# Linked into every Linux binary that loads guest mods (runtime + harnesses).
set(PM_METAL_HAVE_GUEST_PKG_SOURCES 0)
set(PM_METAL_GUEST_PKGS_CMAKE "${PM_METAL_ROOT}/build/guest-pkgs/pkgs.cmake")
if(EXISTS "${PM_METAL_GUEST_PKGS_CMAKE}")
	include(${PM_METAL_GUEST_PKGS_CMAKE})
	if(DEFINED PM_METAL_GUEST_PKG_SOURCES)
		set(PM_METAL_HAVE_GUEST_PKG_SOURCES 1)
	endif()
endif()
if(PM_METAL_HAVE_GUEST_PKG_SOURCES)
	target_sources(pm-linux-runtime PRIVATE ${PM_METAL_GUEST_PKG_SOURCES})
else()
	target_sources(pm-linux-runtime PRIVATE ${PM_COMMON}/mount/pkg_embed_stub.c)
endif()

# Optional anonymous populate embed (verify linux populate only).
set(PM_METAL_POPULATE_EMBED "" CACHE FILEPATH "Optional mount populate embed .c from pack-image.sh")
if(PM_METAL_POPULATE_EMBED AND EXISTS "${PM_METAL_POPULATE_EMBED}")
	target_sources(pm-linux-runtime PRIVATE "${PM_METAL_POPULATE_EMBED}")
endif()

target_include_directories(pm-linux-runtime PRIVATE
	${PM_COMMON_INC}
	${PM_INCLUDE_INC}
	${PM_METAL_WASI_FILE_INCLUDES}
)

# PM_METAL_RUNTIME_MULTI_MODULE: our own flag (not WAMR's WASM_ENABLE_MULTI_
# MODULE — that macro lives in WAMR's internal core/config.h, never exposed
# through the public core/iwasm/include headers this target actually sees)
# gating runtime.c's own module_reader/module_destroyer registration, which
# calls a WAMR API that only *exists* in pm-wamr-vmlib when WAMR_BUILD_MULTI_
# MODULE above is 1 — kept in lockstep with that setting so runtime.c stays
# buildable unchanged against a future zephyr WAMR build with multi-module
# still off (see src/zephyr/CMakeLists.txt, which does not define this).
target_compile_definitions(pm-linux-runtime PRIVATE
	PM_METAL_PORT_TARGET=PM_METAL_PORT_TARGET_LINUX
	PM_METAL_RUNTIME_MULTI_MODULE=1
	PM_METAL_HAVE_CURL=1
)

target_link_libraries(pm-linux-runtime PRIVATE
	pm-wamr-vmlib
	${PM_METAL_NET_LIBS}
	m
	dl
	pthread
)

# ---- pm-linux-thread-stress (opt-in — see scripts/verify linux none threads) ----
# Same common/port/memory sources as pm-linux-runtime, minus main.c, plus a
# pthread-based stress harness. EXCLUDE_FROM_ALL: not part of a normal build.

add_executable(pm-linux-thread-stress EXCLUDE_FROM_ALL
	${PM_METAL_ROOT}/tests/thread_stress_test.c
	pymergetic/metal/port/platform.c
	pymergetic/metal/port/dns.c
	pymergetic/metal/port/tls.c
	pymergetic/metal/port/udp.c
	pymergetic/metal/port/tcp.c
	pymergetic/metal/port/lock.c
	pymergetic/metal/memory/ram.c
	pymergetic/metal/memory/kheap.c
	pymergetic/metal/memory/bytecode.c
	pymergetic/metal/mount/tmpfs.c
	${PM_METAL_WASI_FILE_SRC}
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
)

target_include_directories(pm-linux-thread-stress PRIVATE
	${PM_COMMON_INC}
	${PM_INCLUDE_INC}
	${PM_METAL_WASI_FILE_INCLUDES}
)

target_compile_definitions(pm-linux-thread-stress PRIVATE
	PM_METAL_PORT_TARGET=PM_METAL_PORT_TARGET_LINUX
	PM_METAL_RUNTIME_MULTI_MODULE=1
	PM_METAL_HAVE_CURL=1
)

if(PM_METAL_HAVE_GUEST_PKG_SOURCES)
	target_sources(pm-linux-thread-stress PRIVATE ${PM_METAL_GUEST_PKG_SOURCES})
else()
	target_sources(pm-linux-thread-stress PRIVATE ${PM_COMMON}/mount/pkg_embed_stub.c)
endif()

target_link_libraries(pm-linux-thread-stress PRIVATE
	pm-wamr-vmlib
	${PM_METAL_NET_LIBS}
	m
	dl
	pthread
)

# ---- pm-linux-process-test (opt-in — see scripts/verify linux none process) ----
# runtime/process.h's own spawn()/wait()/kill() end-to-end, incl. worker.c
# (process.c's real dependency, unlike thread-stress above which drives
# runtime.h directly with its own pthreads). EXCLUDE_FROM_ALL: not part of
# a normal build.

add_executable(pm-linux-process-test EXCLUDE_FROM_ALL
	${PM_METAL_ROOT}/tests/process_test.c
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
	${PM_METAL_WASI_FILE_SRC}
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
)

target_include_directories(pm-linux-process-test PRIVATE
	${PM_COMMON_INC}
	${PM_INCLUDE_INC}
	${PM_METAL_WASI_FILE_INCLUDES}
)

target_compile_definitions(pm-linux-process-test PRIVATE
	PM_METAL_PORT_TARGET=PM_METAL_PORT_TARGET_LINUX
	PM_METAL_RUNTIME_MULTI_MODULE=1
	PM_METAL_HAVE_CURL=1
)

if(PM_METAL_HAVE_GUEST_PKG_SOURCES)
	target_sources(pm-linux-process-test PRIVATE ${PM_METAL_GUEST_PKG_SOURCES})
else()
	target_sources(pm-linux-process-test PRIVATE ${PM_COMMON}/mount/pkg_embed_stub.c)
endif()

target_link_libraries(pm-linux-process-test PRIVATE
	pm-wamr-vmlib
	${PM_METAL_NET_LIBS}
	m
	dl
	pthread
)

# WAMR invokeNative_*.s omit .note.GNU-stack; force a non-executable stack on
# the final ELF without patching external/wamr (keeps the tree vanilla).
foreach(_pm_exe pm-linux-runtime pm-linux-thread-stress pm-linux-process-test)
	target_link_options(${_pm_exe} PRIVATE -Wl,-z,noexecstack)
endforeach()
