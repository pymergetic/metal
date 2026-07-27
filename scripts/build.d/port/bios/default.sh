#!/usr/bin/env bash
# Freestanding Multiboot2 Metal BIOS.
# Usage: default.sh [i386|x86_64]
#   x86_64 (default) → build/bios/metal.elf + metal.boot.elf
#   i386             → build/bios/i386/metal.elf (+ pxe drop)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
BIOS="${ROOT}/src/bios"
EFI_METAL="${ROOT}/src/efi/pymergetic/metal"
BIOS_METAL="${BIOS}/pymergetic/metal"
SHARED_METAL="${ROOT}/src/pymergetic/metal"

ARCH="${1:-x86_64}"
case "${ARCH}" in
i386|x86_64) ;;
*)
	echo "bios build: unknown ARCH=${ARCH} (want i386|x86_64)" >&2
	exit 1
	;;
esac

if [[ "${ARCH}" == "i386" ]]; then
	OUT="${ROOT}/build/bios/i386"
	ELF="${OUT}/metal.elf"
	CRT0="${BIOS}/BiosPkg/crt0_i386.S"
	LINK_LD="${BIOS}/BiosPkg/link_i386.ld"
	STACK_S="${BIOS}/shim/stack_switch_i386.S"
	INVOKE_S="${ROOT}/external/wamr/core/iwasm/common/arch/invokeNative_ia32.s"
	MFLAG="-m32"
	MARCH=(-march=i686)
	LD_EMUL="elf_i386"
	BUILD_TARGET="-DBUILD_TARGET_X86_32"
	EXTRA_CFLAGS=()
else
	OUT="${ROOT}/build/bios"
	ELF="${OUT}/metal.elf"
	CRT0="${BIOS}/BiosPkg/crt0.S"
	LINK_LD="${BIOS}/BiosPkg/link.ld"
	STACK_S="${BIOS}/shim/stack_switch.S"
	INVOKE_S="${ROOT}/external/wamr/core/iwasm/common/arch/invokeNative_em64.s"
	MFLAG="-m64"
	MARCH=()
	LD_EMUL="elf_x86_64"
	BUILD_TARGET="-DBUILD_TARGET_X86_64"
	EXTRA_CFLAGS=(-mno-red-zone -mcmodel=kernel)
fi

OBJ="${OUT}/obj"
CC="${CC:-clang}"
LD="${LD:-ld}"

mkdir -p "${OBJ}"

# Bake CA publics (or stub) before compiling trust.c
# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/pki.sh"
pm_metal_pki_bake

# Generated PM_METAL_VERSION (docs/DOC_IFACE_PLAN.md Part P) — build/pm_metal_version.inc.h.
"${ROOT}/scripts/gen_metal_version.sh"

# Regenerate typings/**.pyi (Phase 2e) — editor/linter support only, does
# not affect the firmware build itself.
python3 "${ROOT}/scripts/gen_py_stubs.py" || true

# Scrape every NativeSymbol[] table into the iface sym table
# (docs/DOC_IFACE_PLAN.md Part II-B) — src/pymergetic/metal/util/iface_syms.inc.c.
python3 "${ROOT}/scripts/gen_iface_syms.py"

# MicroPython embed (shared); ports only compile/link.
# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/micropython.sh"
pm_metal_upy_generate_embed

# Embed guest wasm into shared guest path.
if [[ -x "${ROOT}/scripts/build.d/port/efi/embed-mods.sh" ]]; then
	"${ROOT}/scripts/build.d/port/efi/embed-mods.sh" || true
fi

# Build+sign+embed stdlib.zip (mods/py/stdlib_src/) — not tracked in git,
# always freshly baked into the binary, see embed-stdlib.sh. Unlike
# embed-mods.sh above, this has no wasi-sdk dependency, so it's required
# here too (not soft-failed) — both ports need a working stdlib.zip.
"${ROOT}/scripts/build.d/port/efi/embed-stdlib.sh"

# Pack the "metal.guest" (+ "mod.t8_multimod_lib") iface header packs
# (docs/DOC_IFACE_PLAN.md Part II-C) — util/iface_metal_guest_embed.inc.c.
"${ROOT}/scripts/build.d/port/efi/embed-iface.sh"

INCLUDES=(
	-I"$(pm_metal_pki_bake_dir)"
	-I"${ROOT}/build"
	-I"${ROOT}"
	-I"${BIOS}/BiosPkg"
	-I"${BIOS}/shim"
	-I"${SHARED_METAL}/dev/gfx"
	-I"${SHARED_METAL}/dev/net"
	-I"${SHARED_METAL}/dev/net/bge"
	-I"${SHARED_METAL}/net/ip"
	-I"${ROOT}/external/lwip/src/include"
	-I"${ROOT}/external/mbedtls/include"
	-I"${SHARED_METAL}/runtime/mem/host_stubs"
	-I"${SHARED_METAL}/guest/wamr"
	-I"${ROOT}/src/pymergetic/metal"
	-I"${ROOT}/include"
	-I"${ROOT}/external/tlsf"
	-I"${ROOT}/external/microtar/src"
	-I"${ROOT}/external/wamr/core/iwasm/include"
	-I"${ROOT}/external/wamr/core/iwasm/interpreter"
	-I"${ROOT}/external/wamr/core/iwasm/aot"
	-I"${ROOT}/external/wamr/core/iwasm/common"
	-I"${ROOT}/external/wamr/core/iwasm/libraries/libc-wasi"
	-I"${ROOT}/external/wamr/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/include"
	-I"${ROOT}/external/wamr/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/src"
	-I"${ROOT}/external/wamr/core/shared/platform/include"
	-I"${ROOT}/external/wamr/core/shared/platform/common/libc-util"
	-I"${ROOT}/external/wamr/core/shared/mem-alloc"
	-I"${ROOT}/external/wamr/core/shared/utils"
	-I"${ROOT}/external/wamr/core/shared/utils/uncommon"
	-I"${SHARED_METAL}/bus/pci"
	-I"${SHARED_METAL}/py/embed"
	-I"${ROOT}/external/micropython"
	-I"${ROOT}/external/micropython/lib/libm"
	-I"${ROOT}/build/micropython_embed"
	-I"${ROOT}/build/micropython_embed/port"
	-I"${ROOT}/build/micropython_embed/genhdr"
)

CFLAGS=(
	-std=gnu11
	-ffreestanding
	-fno-stack-protector
	-fno-pic
	-fno-pie
	"${MFLAG}"
	"${MARCH[@]}"
	"${EXTRA_CFLAGS[@]}"
	-Wall
	-Wno-error
	-Wno-implicit-function-declaration
	-Wno-unused-parameter
	-Wno-sign-compare
	-Wno-missing-field-initializers
	-Wno-format
	-fno-strict-aliasing
	-DMICROPY_USE_INTERNAL_ERRNO=1
	-DBH_PLATFORM_METAL_BIOS
	-DBH_PLATFORM_METAL_EFI
	# Keep config.h vanilla — override embedded defaults without ZEPHYR masquerade.
	-DAPP_THREAD_STACK_SIZE_DEFAULT=6144
	-DAPP_THREAD_STACK_SIZE_MIN=4096

	"${BUILD_TARGET}"
	-U__linux__
	-Ulinux
	-U__gnu_linux__
	-DWASM_ENABLE_INTERP=1
	-DWASM_ENABLE_FAST_INTERP=1
	-DWASM_ENABLE_AOT=1
	-DWASM_ENABLE_LIBC_WASI=1
	-DWASM_ENABLE_MULTI_MODULE=0
	-DWASM_ENABLE_BULK_MEMORY=1
	-DWASM_ENABLE_SHARED_MEMORY=0
	-DWASM_ENABLE_MINI_LOADER=0
	-DWASM_DISABLE_HW_BOUND_CHECK=1
	-DWASM_DISABLE_STACK_HW_BOUND_CHECK=1
	-DWASM_DISABLE_WAKEUP_BLOCKING_OP=0
	-DWASM_GLOBAL_HEAP_SIZE=50331648
	-DWASM_ENABLE_MODULE_INST_CONTEXT=1
	-DWASM_ENABLE_QUICK_AOT_ENTRY=1
	-DWASM_ENABLE_AOT_INTRINSICS=1
	-DWASM_ENABLE_SHRUNK_MEMORY=1
	-DWASM_ENABLE_EXTENDED_CONST_EXPR=0
	-DBH_MALLOC=wasm_runtime_malloc
	-DBH_FREE=wasm_runtime_free
	-DMBEDTLS_CONFIG_FILE='<pymergetic/metal/net/tls/mbedtls_metal_config.h>'
	${METAL_TRUST_STRICT:+-DPM_METAL_TRUST_STRICT=1}
	"${INCLUDES[@]}"
)

SRCS_C=(
	"${BIOS}/BiosPkg/main.c"
	"${BIOS}/shim/base_memory.c"
	"${BIOS}/shim/base_lib.c"
	"${BIOS}/shim/sync.c"
	"${BIOS}/shim/print.c"
	"${BIOS}/shim/alloc.c"
	"${BIOS}/shim/io.c"
	"${BIOS}/shim/uefi_globals.c"
	"${ROOT}/src/pymergetic/metal/util/fourcc.c"
	"${ROOT}/src/pymergetic/metal/util/eightcc.c"
	"${ROOT}/src/pymergetic/metal/util/arena.c"
	"${ROOT}/src/pymergetic/metal/util/log.c"
	"${ROOT}/src/pymergetic/metal/util/lz4.c"
	"${ROOT}/src/pymergetic/metal/util/tar.c"
	"${ROOT}/src/pymergetic/metal/util/crypto.c"
	"${ROOT}/src/pymergetic/metal/util/ascii.c"
	"${ROOT}/src/pymergetic/metal/util/size.c"
	"${ROOT}/src/pymergetic/metal/util/ip.c"
	"${ROOT}/src/pymergetic/metal/util/doc.c"
	"${ROOT}/src/pymergetic/metal/util/iface.c"
	"${ROOT}/src/pymergetic/metal/util/iface_embed_install.c"
	"${ROOT}/src/pymergetic/metal/trust/trust.c"
	"${ROOT}/src/pymergetic/metal/host/host.c"
	"${ROOT}/src/pymergetic/metal/port/lock.c"
	"${ROOT}/external/lz4/lib/lz4.c"
	"${ROOT}/external/microtar/src/microtar.c"
	"${SHARED_METAL}/util/monocypher_wrap.c"
	"${SHARED_METAL}/runtime/mem/arena.c"
	"${SHARED_METAL}/runtime/mem/mem.c"
	"${SHARED_METAL}/runtime/mem/limit.c"
	"${SHARED_METAL}/runtime/mem/limit_py_bind.c"
	"${SHARED_METAL}/runtime/mem/limit_seed.c"
	"${SHARED_METAL}/runtime/mem/limit_lwip.c"
	"${SHARED_METAL}/runtime/mem/mem_natives.c"
	"${SHARED_METAL}/runtime/mem/libc.c"
	"${SHARED_METAL}/runtime/mem/libc_wamr.c"
	"${SHARED_METAL}/runtime/mem/tlsf_edk2.c"
	"${SHARED_METAL}/runtime/run/run.c"
	"${BIOS_METAL}/runtime/slot/slot_table_port.c"
	"${SHARED_METAL}/runtime/stack/stack.c"
	"${BIOS_METAL}/runtime/stack/stack_port.c"
	"${SHARED_METAL}/runtime/coro/coro.c"
	"${SHARED_METAL}/runtime/coro/coro_timers.c"
	"${SHARED_METAL}/runtime/coro/coro_sleep.c"
	"${SHARED_METAL}/runtime/coro/coro_gather.c"
	"${SHARED_METAL}/runtime/task/task.c"
	"${SHARED_METAL}/runtime/time/time.c"
	"${BIOS_METAL}/runtime/time/time_port.c"
	"${SHARED_METAL}/runtime/async/async.c"
	"${SHARED_METAL}/runtime/async/async_ops.c"
	"${SHARED_METAL}/runtime/async/async_natives.c"
	"${SHARED_METAL}/runtime/async/async_session.c"
	"${SHARED_METAL}/runtime/async/async_py_bind.c"
	"${SHARED_METAL}/bus/io/io.c"
	"${SHARED_METAL}/bus/pci/pci.c"
	"${SHARED_METAL}/bus/virtio/virtio_pci.c"
	"${BIOS_METAL}/bus/virtio/virtio_pci_port.c"
	"${BIOS_METAL}/boot/run_port.c"
	"${BIOS_METAL}/boot/bios/boot_init.c"
	"${SHARED_METAL}/boot/boot_init.c"
	"${SHARED_METAL}/boot/boot_test.c"
	"${SHARED_METAL}/boot/boot_python.c"
	"${SHARED_METAL}/boot/banner.c"
	"${SHARED_METAL}/boot/authors.c"
	"${SHARED_METAL}/boot/authors_py_bind.c"
	"${SHARED_METAL}/boot/externals.c"
	"${SHARED_METAL}/boot/externals_py_bind.c"
	"${SHARED_METAL}/boot/boot_harvest.c"
	"${SHARED_METAL}/boot/boot_shell.c"
	"${SHARED_METAL}/log/log.c"
	"${BIOS_METAL}/log/log_port.c"
	"${SHARED_METAL}/dev/console/virtio_console.c"
	"${SHARED_METAL}/dev/blk/blk.c"
	"${SHARED_METAL}/dev/blk/virtio_blk.c"
	"${SHARED_METAL}/dev/blk/ide_ata.c"
	"${SHARED_METAL}/dev/audio/audio.c"
	"${SHARED_METAL}/dev/audio/audio_null.c"
	"${SHARED_METAL}/dev/audio/virtio_snd.c"
	"${SHARED_METAL}/dev/audio/ac97.c"
	"${SHARED_METAL}/dev/audio/audio_shell.c"
	"${SHARED_METAL}/dev/audio/audio_py_bind.c"
	"${SHARED_METAL}/dev/acpi/acpi_power.c"
	"${SHARED_METAL}/dev/gfx/gfx.c"
	"${SHARED_METAL}/dev/gfx/scanout.c"
	"${SHARED_METAL}/dev/gfx/scanout_bochs.c"
	"${SHARED_METAL}/dev/gfx/scanout_lfb_copy.c"
	"${SHARED_METAL}/dev/gfx/scanout_gop_blt.c"
	"${BIOS_METAL}/dev/gfx/scanout_gop_blt_port.c"
	"${SHARED_METAL}/dev/gfx/scanout_virtio_gpu.c"
	"${SHARED_METAL}/dev/gfx/scanout_radeon_rv370.c"
	"${SHARED_METAL}/dev/gfx/scanout_i915_855gm.c"
	"${BIOS_METAL}/dev/gfx/gfx_port.c"
	"${SHARED_METAL}/dev/input/input.c"
	"${SHARED_METAL}/dev/input/keyb.c"
	"${SHARED_METAL}/dev/input/keyb_layout/keyb_layout_us.c"
	"${SHARED_METAL}/dev/input/keyb_layout/keyb_layout_de.c"
	"${SHARED_METAL}/dev/input/virtio_input.c"
	"${BIOS_METAL}/dev/input/input_port.c"
	"${SHARED_METAL}/dev/stream/stream.c"
	"${SHARED_METAL}/dev/random/random.c"
	"${BIOS_METAL}/dev/random/random_port.c"
	"${SHARED_METAL}/net/ip/ip.c"
	"${SHARED_METAL}/net/ip/ip_null.c"
	"${SHARED_METAL}/dev/net/virtio_net.c"
	"${SHARED_METAL}/dev/net/bge/bge_netif.c"
	"${SHARED_METAL}/dev/net/bge/bge_metal.c"
	"${BIOS_METAL}/dev/net/bge/bge_metal_port.c"
	"${SHARED_METAL}/net/ip/ip_lwip.c"
	"${SHARED_METAL}/net/ip/metal_dhcp6_stateful.c"
	"${SHARED_METAL}/net/ip/lwip_sys.c"
	"${SHARED_METAL}/net/tls/mbedtls_metal_platform.c"
	"${SHARED_METAL}/net/tls/tls.c"
	"${SHARED_METAL}/net/ping/ping.c"
	"${SHARED_METAL}/net/http/http.c"
	"${SHARED_METAL}/net/http/http_parse.c"
	"${SHARED_METAL}/net/asgi/asgi_registry.c"
	"${SHARED_METAL}/net/asgi/asgi_apps.c"
	"${SHARED_METAL}/net/asgi/asgi_config.c"
	"${SHARED_METAL}/net/asgi/asgi_ws.c"
	"${SHARED_METAL}/net/asgi/asgi_server.c"
	"${SHARED_METAL}/net/asgi/asgi_shell.c"
	"${SHARED_METAL}/net/ssh/ssh_server.c"
	"${SHARED_METAL}/net/ssh/ssh_shell.c"
	"${SHARED_METAL}/net/ssh/ssh_config.c"
	"${SHARED_METAL}/net/ssh/ssh_py_bind.c"
	"${SHARED_METAL}/net/ssh/ssh_dropbear.c"
	"${SHARED_METAL}/net/ssh/dropbear_fd.c"
	"${SHARED_METAL}/net/ssh/dropbear_posix.c"
	"${SHARED_METAL}/auth/auth.c"
	"${SHARED_METAL}/auth/bcrypt_wrap.c"
	"${SHARED_METAL}/net/tftp/tftp.c"
	"${SHARED_METAL}/net/ntp/ntp.c"
	"${SHARED_METAL}/net/ip/ip_life.c"
	"${ROOT}/external/lwip/src/core/init.c"
	"${ROOT}/external/lwip/src/core/def.c"
	"${ROOT}/external/lwip/src/core/dns.c"
	"${ROOT}/external/lwip/src/core/inet_chksum.c"
	"${ROOT}/external/lwip/src/core/ip.c"
	"${ROOT}/external/lwip/src/core/mem.c"
	"${ROOT}/external/lwip/src/core/memp.c"
	"${ROOT}/external/lwip/src/core/netif.c"
	"${ROOT}/external/lwip/src/core/pbuf.c"
	"${ROOT}/external/lwip/src/core/stats.c"
	"${ROOT}/external/lwip/src/core/sys.c"
	"${ROOT}/external/lwip/src/core/tcp.c"
	"${ROOT}/external/lwip/src/core/tcp_in.c"
	"${ROOT}/external/lwip/src/core/tcp_out.c"
	"${ROOT}/external/lwip/src/core/timeouts.c"
	"${ROOT}/external/lwip/src/core/udp.c"
	"${ROOT}/external/lwip/src/core/raw.c"
	"${ROOT}/external/lwip/src/core/ipv4/etharp.c"
	"${ROOT}/external/lwip/src/core/ipv4/icmp.c"
	"${ROOT}/external/lwip/src/core/ipv4/ip4.c"
	"${ROOT}/external/lwip/src/core/ipv4/ip4_addr.c"
	"${ROOT}/external/lwip/src/core/ipv4/ip4_frag.c"
	"${ROOT}/external/lwip/src/core/ipv4/acd.c"
	"${ROOT}/external/lwip/src/core/ipv4/dhcp.c"
	"${ROOT}/external/lwip/src/core/ipv6/icmp6.c"
	"${ROOT}/external/lwip/src/core/ipv6/inet6.c"
	"${ROOT}/external/lwip/src/core/ipv6/ip6_addr.c"
	"${ROOT}/external/lwip/src/core/ipv6/ip6.c"
	"${ROOT}/external/lwip/src/core/ipv6/ip6_frag.c"
	"${ROOT}/external/lwip/src/core/ipv6/mld6.c"
	"${ROOT}/external/lwip/src/core/ipv6/nd6.c"
	"${ROOT}/external/lwip/src/core/ipv6/ethip6.c"
	"${ROOT}/external/lwip/src/core/ipv6/dhcp6.c"
	"${ROOT}/external/lwip/src/netif/ethernet.c"
	"${SHARED_METAL}/fs/esp/esp.c"
	"${BIOS_METAL}/fs/esp/esp_port.c"
	"${SHARED_METAL}/fs/fs.c"
	"${SHARED_METAL}/shell/ui/shell.c"
	"${SHARED_METAL}/shell/ui/widget.c"
	"${SHARED_METAL}/shell/ui/paint.c"
	"${SHARED_METAL}/shell/ui/tabs.c"
	"${SHARED_METAL}/shell/ui/input.c"
	"${SHARED_METAL}/shell/ui/native.c"
	"${SHARED_METAL}/shell/shell/shell.c"
	"${SHARED_METAL}/shell/shell/shell_cmd.c"
	"${SHARED_METAL}/shell/shell/shell_core_cmds.c"
	"${SHARED_METAL}/shell/shell/shell_py_bind.c"
	"${SHARED_METAL}/util/iface_shell.c"
	"${SHARED_METAL}/py/mphalport_metal.c"
	"${SHARED_METAL}/py/py.c"
	"${SHARED_METAL}/py/py_bind.c"
	"${SHARED_METAL}/py/py_ctx.c"
	"${SHARED_METAL}/py/py_obj.c"
	"${SHARED_METAL}/py/py_await.c"
	"${SHARED_METAL}/py/py_shell.c"
	"${SHARED_METAL}/py/py_zip.c"
	"${SHARED_METAL}/py/py_zip_embed.c"
	"${SHARED_METAL}/py/py_zip_read.c"
	"${SHARED_METAL}/py/py_guest.c"
	"${SHARED_METAL}/py/py_port_stubs.c"
"${SHARED_METAL}/fs/fs_py_bind.c"
"${SHARED_METAL}/dev/random/random_py_bind.c"
"${SHARED_METAL}/dev/random/time_py_bind.c"
"${SHARED_METAL}/util/tar_py_bind.c"
"${SHARED_METAL}/util/doc_py_bind.c"
"${SHARED_METAL}/util/iface_py_bind.c"
"${SHARED_METAL}/net/tls/tls_conn.c"
"${SHARED_METAL}/net/tls/tls_py_bind.c"
"${SHARED_METAL}/net/ip/ip_py_bind.c"
"${SHARED_METAL}/net/http/http_py_bind.c"
"${SHARED_METAL}/net/asgi/asgi_py_bind.c"
"${ROOT}/external/micropython/extmod/modbinascii.c"
"${ROOT}/external/micropython/extmod/modrandom.c"
"${ROOT}/external/micropython/extmod/modhashlib.c"
"${ROOT}/external/micropython/extmod/modre.c"
"${ROOT}/external/micropython/extmod/moddeflate.c"
"${ROOT}/external/micropython/extmod/modjson.c"
"${SHARED_METAL}/py/py_libm_extra.c"
"${SHARED_METAL}/py/py_libm_math.c"
"${ROOT}/external/micropython/lib/libm/acoshf.c"
"${ROOT}/external/micropython/lib/libm/asinfacosf.c"
"${ROOT}/external/micropython/lib/libm/asinhf.c"
"${ROOT}/external/micropython/lib/libm/atan2f.c"
"${ROOT}/external/micropython/lib/libm/atanf.c"
"${ROOT}/external/micropython/lib/libm/atanhf.c"
"${ROOT}/external/micropython/lib/libm/ef_rem_pio2.c"
"${ROOT}/external/micropython/lib/libm/erf_lgamma.c"
"${ROOT}/external/micropython/lib/libm/fmodf.c"
"${ROOT}/external/micropython/lib/libm/kf_cos.c"
"${ROOT}/external/micropython/lib/libm/kf_rem_pio2.c"
"${ROOT}/external/micropython/lib/libm/kf_sin.c"
"${ROOT}/external/micropython/lib/libm/kf_tan.c"
"${ROOT}/external/micropython/lib/libm/log1pf.c"
"${ROOT}/external/micropython/lib/libm/nearbyintf.c"
"${ROOT}/external/micropython/lib/libm/roundf.c"
"${ROOT}/external/micropython/lib/libm/sf_cos.c"
"${ROOT}/external/micropython/lib/libm/sf_erf.c"
"${ROOT}/external/micropython/lib/libm/sf_frexp.c"
"${ROOT}/external/micropython/lib/libm/sf_ldexp.c"
"${ROOT}/external/micropython/lib/libm/sf_modf.c"
"${ROOT}/external/micropython/lib/libm/sf_sin.c"
"${ROOT}/external/micropython/lib/libm/sf_tan.c"
"${ROOT}/external/micropython/lib/libm/wf_lgamma.c"
"${ROOT}/external/micropython/lib/libm/wf_tgamma.c"
	"${SHARED_METAL}/net/ip/ip_shell.c"
	"${SHARED_METAL}/dev/input/input_shell.c"
	"${SHARED_METAL}/shell/hwinfo/hwinfo.c"
	"${SHARED_METAL}/shell/lifecycle/lifecycle.c"
	"${SHARED_METAL}/guest/process/process.c"
	"${SHARED_METAL}/guest/process/process_py_bind.c"
	"${SHARED_METAL}/guest/mod/mod.c"
	"${SHARED_METAL}/guest/mod/mod_py_bind.c"
	"${SHARED_METAL}/guest/pkg/pkg.c"
	"${SHARED_METAL}/guest/wasm/wasm.c"
	"${SHARED_METAL}/guest/wamr/efi_platform.c"
	"${SHARED_METAL}/guest/wamr/efi_thread.c"
	"${SHARED_METAL}/guest/wamr/efi_socket.c"
	"${SHARED_METAL}/guest/wamr/efi_wasi_fs.c"
	"${ROOT}/external/wamr/core/shared/platform/common/math/math.c"
	"${ROOT}/external/wamr/core/shared/mem-alloc/ems/ems_alloc.c"
	"${ROOT}/external/wamr/core/shared/mem-alloc/ems/ems_gc.c"
	"${ROOT}/external/wamr/core/shared/mem-alloc/ems/ems_hmu.c"
	"${ROOT}/external/wamr/core/shared/mem-alloc/ems/ems_kfc.c"
	"${ROOT}/external/wamr/core/shared/mem-alloc/mem_alloc.c"
	"${ROOT}/external/wamr/core/shared/utils/bh_assert.c"
	"${ROOT}/external/wamr/core/shared/utils/bh_bitmap.c"
	"${ROOT}/external/wamr/core/shared/utils/bh_common.c"
	"${ROOT}/external/wamr/core/shared/utils/bh_hashmap.c"
	"${ROOT}/external/wamr/core/shared/utils/bh_leb128.c"
	"${ROOT}/external/wamr/core/shared/utils/bh_list.c"
	"${ROOT}/external/wamr/core/shared/utils/bh_log.c"
	"${ROOT}/external/wamr/core/shared/utils/bh_queue.c"
	"${ROOT}/external/wamr/core/shared/utils/bh_vector.c"
	"${ROOT}/external/wamr/core/shared/utils/runtime_timer.c"
	"${ROOT}/external/wamr/core/iwasm/libraries/libc-wasi/libc_wasi_wrapper.c"
	"${ROOT}/external/wamr/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/src/blocking_op.c"
	"${ROOT}/external/wamr/core/shared/platform/common/libc-util/libc_errno.c"
	"${ROOT}/external/wamr/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/src/posix.c"
	"${ROOT}/external/wamr/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/src/random.c"
	"${ROOT}/external/wamr/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/src/str.c"
	"${ROOT}/external/wamr/core/iwasm/common/wasm_application.c"
	"${ROOT}/external/wamr/core/iwasm/common/wasm_blocking_op.c"
	"${ROOT}/external/wamr/core/iwasm/common/wasm_c_api.c"
	"${ROOT}/external/wamr/core/iwasm/common/wasm_exec_env.c"
	"${ROOT}/external/wamr/core/iwasm/common/wasm_loader_common.c"
	"${ROOT}/external/wamr/core/iwasm/common/wasm_memory.c"
	"${ROOT}/external/wamr/core/iwasm/common/wasm_native.c"
	"${ROOT}/external/wamr/core/iwasm/common/wasm_runtime_common.c"
	"${ROOT}/external/wamr/core/iwasm/common/wasm_shared_memory.c"
	"${ROOT}/external/wamr/core/iwasm/interpreter/wasm_interp_fast.c"
	"${ROOT}/external/wamr/core/iwasm/interpreter/wasm_loader.c"
	"${ROOT}/external/wamr/core/iwasm/interpreter/wasm_runtime.c"
	"${ROOT}/external/wamr/core/iwasm/aot/aot_intrinsic.c"
	"${ROOT}/external/wamr/core/iwasm/aot/aot_loader.c"
	"${ROOT}/external/wamr/core/iwasm/aot/aot_runtime.c"
)
if [[ "${ARCH}" == "i386" ]]; then
	SRCS_C+=("${ROOT}/external/wamr/core/iwasm/aot/arch/aot_reloc_x86_32.c")
else
	SRCS_C+=("${ROOT}/external/wamr/core/iwasm/aot/arch/aot_reloc_x86_64.c")
fi

mapfile -t UPY_SRCS < <(pm_metal_upy_embed_c_sources "${ARCH}" || true)
SRCS_C+=("${UPY_SRCS[@]}")

mapfile -t MBEDTLS_SRCS < <(
	grep -E 'external/mbedtls/library/' "${ROOT}/src/efi/MetalPkg/Metal.inf" \
		| sed -E "s@^[[:space:]]+\.\./\.\./\.\./@${ROOT}/@"
)
SRCS_C+=("${MBEDTLS_SRCS[@]}")

SRCS_S=(
	"${CRT0}"
	"${STACK_S}"
	"${INVOKE_S}"
)

# FB detectors (always): Multiboot parse + Bochs + VESA. INT10 bounce is
# bitness plumbing only — x86_64 long mode uses a miss stub until LM→RM exists.
SRCS_C+=(
	"${BIOS}/BiosPkg/fb_bochs.c"
	"${BIOS}/BiosPkg/vesa.c"
)
if [[ "${ARCH}" == "i386" ]]; then
	SRCS_S+=("${BIOS}/BiosPkg/vesa_rm_i386.S")
else
	SRCS_C+=("${BIOS}/BiosPkg/vesa_rm_stub.c")
fi

DROPBEAR_EXTRA_CFLAGS=(
	-I"${SHARED_METAL}/net/ssh/dropbear_metal"
	-I"${SHARED_METAL}/net/ssh/dropbear_stubs"
	-I"${ROOT}/external/dropbear/src"
	-DDROPBEAR_METAL=1
	-DLOCALOPTIONS_H_EXISTS=1
)

OBJS=()
echo "bios build (${ARCH}): compiling ($CC ${MFLAG})"
for src in "${SRCS_C[@]}"; do
	base="$(basename "${src}" .c)"
	hash="$(printf '%s' "${src}" | md5sum | cut -c1-8)"
	obj="${OBJ}/${base}-${hash}.o"
	extra=()
	case "${base}" in
	dropbear_posix|dropbear_fd|ssh_dropbear)
		# Stubs must precede host_stubs so arpa/inet.h / netinet win.
		extra=("${DROPBEAR_EXTRA_CFLAGS[@]}")
		"${CC}" "${extra[@]}" "${CFLAGS[@]}" -c "${src}" -o "${obj}"
		;;
	*)
		"${CC}" "${CFLAGS[@]}" -c "${src}" -o "${obj}"
		;;
	esac
	OBJS+=("${obj}")
done
for src in "${SRCS_S[@]}"; do
	base="$(basename "${src}")"
	hash="$(printf '%s' "${src}" | md5sum | cut -c1-8)"
	obj="${OBJ}/${base}-${hash}.o"
	"${CC}" "${MFLAG}" -c "${src}" -o "${obj}"
	OBJS+=("${obj}")
done

echo "bios build (${ARCH}): linking ${ELF}"
CRT0_OBJ=""
OTHER_OBJS=()
CRT0_BASE="$(basename "${CRT0}")"
for o in "${OBJS[@]}"; do
	if [[ "$(basename "${o}")" == "${CRT0_BASE}"-* ]]; then
		CRT0_OBJ="${o}"
	else
		OTHER_OBJS+=("${o}")
	fi
done
# Dropbear static lib (SSH server crypto/session).
"${ROOT}/scripts/build.d/lib/dropbear.sh" "${ARCH}"
DROPBEAR_LIB="${ROOT}/build/dropbear/${ARCH}/libdropbear_metal.a"

LIBGCC="$("${CC}" "${MFLAG}" -print-libgcc-file-name)"
"${LD}" -m "${LD_EMUL}" -nostdlib -static -z noexecstack -T "${LINK_LD}" -o "${ELF}" \
	"${CRT0_OBJ}" "${OTHER_OBJS[@]}" "${DROPBEAR_LIB}" "${LIBGCC}"

# Optional Kernel-CA detached signature (host PKI; skipped when mode=off).
if pm_metal_pki_want_sign && [[ -f "$(pm_metal_pki_dir)/kernel/ca.key" ]]; then
	"${ROOT}/scripts/pki" sign-elf "${ELF}" || true
fi

if [[ "${ARCH}" == "x86_64" ]]; then
	# ELF32 Multiboot trampoline embeds metal.elf — QEMU -kernel cannot load ELF64.
	TRAMP_ELF="${OUT}/metal.boot.elf"
	"${CC}" -m32 -ffreestanding -fno-pic -fno-stack-protector -c \
		"${BIOS}/BiosPkg/trampoline_load.c" -o "${OBJ}/trampoline_load.o"
	"${CC}" -m32 -c "${BIOS}/BiosPkg/trampoline32.S" -o "${OBJ}/trampoline32.o"
	"${CC}" -m64 -c "${BIOS}/BiosPkg/trampoline64.S" -o "${OBJ}/trampoline64_64.o"
	objcopy -O elf32-i386 "${OBJ}/trampoline64_64.o" "${OBJ}/trampoline64.o"
	"${LD}" -m elf_i386 -r -b binary -o "${OBJ}/metal_bin.o" "${ELF}"
	BIN_START="$(nm "${OBJ}/metal_bin.o" | awk '/_start$/{print $3; exit}')"
	BIN_END="$(nm "${OBJ}/metal_bin.o" | awk '/_end$/{print $3; exit}')"
	if [[ -z "${BIN_START}" || -z "${BIN_END}" ]]; then
		echo "bios build: failed to find binary blob symbols" >&2
		nm "${OBJ}/metal_bin.o" >&2 || true
		exit 1
	fi
	objcopy \
		--redefine-sym "${BIN_START}=_binary_metal_elf_start" \
		--redefine-sym "${BIN_END}=_binary_metal_elf_end" \
		"${OBJ}/metal_bin.o" "${OBJ}/metal_bin_named.o"
	"${LD}" -m elf_i386 -nostdlib -static -z noexecstack \
		-T "${BIOS}/BiosPkg/link32.ld" \
		-o "${TRAMP_ELF}" \
		"${OBJ}/trampoline32.o" "${OBJ}/trampoline64.o" \
		"${OBJ}/trampoline_load.o" "${OBJ}/metal_bin_named.o"

	echo "bios build: ok → ${ELF} + ${TRAMP_ELF}"
	ls -la "${ELF}" "${TRAMP_ELF}"

	ISO_DIR="${OUT}/iso"
	ISO="${OUT}/metal.iso"
	if command -v grub-mkrescue >/dev/null 2>&1 && command -v xorriso >/dev/null 2>&1; then
		rm -rf "${ISO_DIR}"
		mkdir -p "${ISO_DIR}/boot/grub"
		cp -f "${ELF}" "${ISO_DIR}/boot/metal.elf"
		cat >"${ISO_DIR}/boot/grub/grub.cfg" <<'EOF'
set timeout=0
set default=0
menuentry "pymergetic metal bios" {
	multiboot2 /boot/metal.elf
	boot
}
EOF
		grub-mkrescue -o "${ISO}" "${ISO_DIR}" >/dev/null 2>&1 || true
	fi
else
	echo "bios build: ok → ${ELF}"
	ls -la "${ELF}"
	file "${ELF}"
	if [[ -x "${ROOT}/scripts/build.d/port/bios/pxe.sh" ]]; then
		"${ROOT}/scripts/build.d/port/bios/pxe.sh" "${ELF}"
	fi
fi
