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
	INVOKE_S="${ROOT}/src/efi/wamr/core/iwasm/common/arch/invokeNative_ia32.s"
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
	INVOKE_S="${ROOT}/src/efi/wamr/core/iwasm/common/arch/invokeNative_em64.s"
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

# Embed guest wasm into shared guest path.
if [[ -x "${ROOT}/scripts/build.d/port/efi/embed-mods.sh" ]]; then
	"${ROOT}/scripts/build.d/port/efi/embed-mods.sh" || true
fi

INCLUDES=(
	-I"${ROOT}"
	-I"${BIOS}/BiosPkg"
	-I"${BIOS}/shim"
	-I"${SHARED_METAL}/dev/gfx"
	-I"${SHARED_METAL}/dev/net"
	-I"${SHARED_METAL}/dev/net/bge"
	-I"${ROOT}/external/lwip/src/include"
	-I"${ROOT}/external/mbedtls/include"
	-I"${SHARED_METAL}/runtime/mem/host_stubs"
	-I"${SHARED_METAL}/guest/wamr"
	-I"${ROOT}/src/pymergetic/metal"
	-I"${ROOT}/include"
	-I"${ROOT}/external/tlsf"
	-I"${ROOT}/external/microtar/src"
	-I"${ROOT}/src/efi/wamr/core/iwasm/include"
	-I"${ROOT}/src/efi/wamr/core/iwasm/interpreter"
	-I"${ROOT}/src/efi/wamr/core/iwasm/common"
	-I"${ROOT}/src/efi/wamr/core/iwasm/libraries/libc-wasi"
	-I"${ROOT}/src/efi/wamr/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/include"
	-I"${ROOT}/src/efi/wamr/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/src"
	-I"${ROOT}/src/efi/wamr/core/shared/platform/include"
	-I"${ROOT}/src/efi/wamr/core/shared/platform/common/libc-util"
	-I"${ROOT}/src/efi/wamr/core/shared/mem-alloc"
	-I"${ROOT}/src/efi/wamr/core/shared/utils"
	-I"${ROOT}/src/efi/wamr/core/shared/utils/uncommon"
	-I"${SHARED_METAL}/bus/pci"
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
	-DBH_PLATFORM_METAL_BIOS
	-DBH_PLATFORM_METAL_EFI
	-DBH_PLATFORM_ZEPHYR
	"${BUILD_TARGET}"
	-U__linux__
	-Ulinux
	-U__gnu_linux__
	-DWASM_ENABLE_INTERP=1
	-DWASM_ENABLE_FAST_INTERP=1
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
	-DWASM_ENABLE_QUICK_AOT_ENTRY=0
	-DWASM_ENABLE_AOT_INTRINSICS=0
	-DWASM_ENABLE_SHRUNK_MEMORY=1
	-DWASM_ENABLE_EXTENDED_CONST_EXPR=0
	-DBH_MALLOC=wasm_runtime_malloc
	-DBH_FREE=wasm_runtime_free
	-DMBEDTLS_CONFIG_FILE='<pymergetic/metal/dev/net/mbedtls_metal_config.h>'
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
	"${ROOT}/src/pymergetic/metal/port/lock.c"
	"${ROOT}/external/lz4/lib/lz4.c"
	"${ROOT}/external/microtar/src/microtar.c"
	"${SHARED_METAL}/util/monocypher_wrap.c"
	"${SHARED_METAL}/runtime/mem/arena.c"
	"${SHARED_METAL}/runtime/mem/mem.c"
	"${SHARED_METAL}/runtime/mem/libc.c"
	"${SHARED_METAL}/runtime/mem/libc_wamr.c"
	"${SHARED_METAL}/runtime/mem/tlsf_edk2.c"
	"${SHARED_METAL}/runtime/run/run.c"
	"${SHARED_METAL}/runtime/stack/stack.c"
	"${SHARED_METAL}/runtime/coro/coro.c"
	"${SHARED_METAL}/runtime/task/task.c"
	"${SHARED_METAL}/runtime/time/time.c"
	"${BIOS_METAL}/runtime/time/time_port.c"
	"${SHARED_METAL}/runtime/async/async.c"
	"${SHARED_METAL}/bus/io/io.c"
	"${SHARED_METAL}/bus/pci/pci.c"
	"${SHARED_METAL}/bus/virtio/virtio_pci.c"
	"${BIOS_METAL}/boot/run_port.c"
	"${BIOS_METAL}/boot/bios/boot_init.c"
	"${SHARED_METAL}/boot/banner.c"
	"${SHARED_METAL}/log/log.c"
	"${SHARED_METAL}/dev/console/virtio_console.c"
	"${SHARED_METAL}/dev/blk/blk.c"
	"${SHARED_METAL}/dev/blk/virtio_blk.c"
	"${SHARED_METAL}/dev/blk/ide_ata.c"
	"${SHARED_METAL}/dev/audio/audio.c"
	"${SHARED_METAL}/dev/audio/audio_null.c"
	"${SHARED_METAL}/dev/audio/virtio_snd.c"
	"${SHARED_METAL}/dev/gfx/gfx.c"
	"${BIOS_METAL}/dev/gfx/gfx_port.c"
	"${SHARED_METAL}/dev/input/input.c"
	"${SHARED_METAL}/dev/input/keyb.c"
	"${BIOS_METAL}/dev/input/input_port.c"
	"${SHARED_METAL}/dev/stream/stream.c"
	"${SHARED_METAL}/dev/random/random.c"
	"${BIOS_METAL}/dev/random/random_port.c"
	"${SHARED_METAL}/dev/net/net.c"
	"${SHARED_METAL}/dev/net/net_null.c"
	"${SHARED_METAL}/dev/net/virtio_net.c"
	"${SHARED_METAL}/dev/net/bge/bge_netif.c"
	"${SHARED_METAL}/dev/net/bge/bge_metal.c"
	"${SHARED_METAL}/dev/net/net_lwip.c"
	"${SHARED_METAL}/dev/net/metal_dhcp6_stateful.c"
	"${SHARED_METAL}/dev/net/lwip_sys.c"
	"${SHARED_METAL}/dev/net/mbedtls_metal_platform.c"
	"${SHARED_METAL}/dev/net/tls.c"
	"${SHARED_METAL}/dev/net/ping.c"
	"${SHARED_METAL}/dev/net/http.c"
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
	"${SHARED_METAL}/shell/ui/ui.c"
	"${SHARED_METAL}/shell/shell/shell.c"
	"${SHARED_METAL}/shell/shell/shell_cmd.c"
	"${SHARED_METAL}/shell/shell/shell_core_cmds.c"
	"${SHARED_METAL}/dev/net/net_shell.c"
	"${SHARED_METAL}/dev/input/input_shell.c"
	"${SHARED_METAL}/shell/hwinfo/hwinfo.c"
	"${SHARED_METAL}/shell/lifecycle/lifecycle.c"
	"${SHARED_METAL}/guest/wasm/wasm.c"
	"${SHARED_METAL}/guest/wamr/efi_platform.c"
	"${SHARED_METAL}/guest/wamr/efi_thread.c"
	"${SHARED_METAL}/guest/wamr/efi_socket.c"
	"${SHARED_METAL}/guest/wamr/efi_wasi_fs.c"
	"${ROOT}/src/efi/wamr/core/shared/platform/common/math/math.c"
	"${ROOT}/src/efi/wamr/core/shared/mem-alloc/ems/ems_alloc.c"
	"${ROOT}/src/efi/wamr/core/shared/mem-alloc/ems/ems_gc.c"
	"${ROOT}/src/efi/wamr/core/shared/mem-alloc/ems/ems_hmu.c"
	"${ROOT}/src/efi/wamr/core/shared/mem-alloc/ems/ems_kfc.c"
	"${ROOT}/src/efi/wamr/core/shared/mem-alloc/mem_alloc.c"
	"${ROOT}/src/efi/wamr/core/shared/utils/bh_assert.c"
	"${ROOT}/src/efi/wamr/core/shared/utils/bh_bitmap.c"
	"${ROOT}/src/efi/wamr/core/shared/utils/bh_common.c"
	"${ROOT}/src/efi/wamr/core/shared/utils/bh_hashmap.c"
	"${ROOT}/src/efi/wamr/core/shared/utils/bh_leb128.c"
	"${ROOT}/src/efi/wamr/core/shared/utils/bh_list.c"
	"${ROOT}/src/efi/wamr/core/shared/utils/bh_log.c"
	"${ROOT}/src/efi/wamr/core/shared/utils/bh_queue.c"
	"${ROOT}/src/efi/wamr/core/shared/utils/bh_vector.c"
	"${ROOT}/src/efi/wamr/core/shared/utils/runtime_timer.c"
	"${ROOT}/src/efi/wamr/core/iwasm/libraries/libc-wasi/libc_wasi_wrapper.c"
	"${ROOT}/src/efi/wamr/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/src/blocking_op.c"
	"${ROOT}/src/efi/wamr/core/shared/platform/common/libc-util/libc_errno.c"
	"${ROOT}/src/efi/wamr/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/src/posix.c"
	"${ROOT}/src/efi/wamr/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/src/random.c"
	"${ROOT}/src/efi/wamr/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/src/str.c"
	"${ROOT}/src/efi/wamr/core/iwasm/common/wasm_application.c"
	"${ROOT}/src/efi/wamr/core/iwasm/common/wasm_blocking_op.c"
	"${ROOT}/src/efi/wamr/core/iwasm/common/wasm_c_api.c"
	"${ROOT}/src/efi/wamr/core/iwasm/common/wasm_exec_env.c"
	"${ROOT}/src/efi/wamr/core/iwasm/common/wasm_loader_common.c"
	"${ROOT}/src/efi/wamr/core/iwasm/common/wasm_memory.c"
	"${ROOT}/src/efi/wamr/core/iwasm/common/wasm_native.c"
	"${ROOT}/src/efi/wamr/core/iwasm/common/wasm_runtime_common.c"
	"${ROOT}/src/efi/wamr/core/iwasm/common/wasm_shared_memory.c"
	"${ROOT}/src/efi/wamr/core/iwasm/interpreter/wasm_interp_fast.c"
	"${ROOT}/src/efi/wamr/core/iwasm/interpreter/wasm_loader.c"
	"${ROOT}/src/efi/wamr/core/iwasm/interpreter/wasm_runtime.c"
)

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

OBJS=()
echo "bios build (${ARCH}): compiling ($CC ${MFLAG})"
for src in "${SRCS_C[@]}"; do
	base="$(basename "${src}" .c)"
	hash="$(printf '%s' "${src}" | md5sum | cut -c1-8)"
	obj="${OBJ}/${base}-${hash}.o"
	"${CC}" "${CFLAGS[@]}" -c "${src}" -o "${obj}"
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
LIBGCC="$("${CC}" "${MFLAG}" -print-libgcc-file-name)"
"${LD}" -m "${LD_EMUL}" -nostdlib -static -z noexecstack -T "${LINK_LD}" -o "${ELF}" \
	"${CRT0_OBJ}" "${OTHER_OBJS[@]}" "${LIBGCC}"

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
