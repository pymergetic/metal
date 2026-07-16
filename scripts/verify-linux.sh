#!/usr/bin/env bash
# init -> load -> run -> unload -> shutdown, on linux, using the dynamic
# loader API — two mods, one process, one shared vfs_root.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

"${ROOT}/scripts/build-mod.sh"
"${ROOT}/scripts/build-linux.sh"

RUNTIME="${ROOT}/build/linux/runtime/pm-linux-runtime"
if [ ! -x "${RUNTIME}" ]; then
	echo "missing ${RUNTIME}" >&2
	exit 1
fi

VFS_ROOT="$(mktemp -d)"
trap 'rm -rf "${VFS_ROOT}"' EXIT

printf 'hello from vfs root\n' > "${VFS_ROOT}/README"

# .wasm files are loaded from inside the VFS root, same as any other guest
# path — the loader resolves them against --vfs-root, never a host path
# outside the tree, so this works unchanged when vfs_root is a mounted
# ramdisk on zephyr instead of a host dir.
mkdir -p "${VFS_ROOT}/mods"
cp "${ROOT}/build/mods/t0_hello.wasm" "${ROOT}/build/mods/t1_read.wasm" \
	"${ROOT}/build/mods/t3_util_native.wasm" \
	"${ROOT}/build/mods/t4_getpid.wasm" \
	"${ROOT}/build/mods/t8_multimod_lib.wasm" "${ROOT}/build/mods/t9_multimod_app.wasm" \
	"${ROOT}/build/mods/t23_pthread.wasm" \
	"${VFS_ROOT}/mods/"

OUT="$("${RUNTIME}" --memory=16777216 --bytecode-memory=1048576 --vfs-root="${VFS_ROOT}" \
	/mods/t0_hello.wasm \
	/mods/t1_read.wasm \
	/mods/t3_util_native.wasm \
	/mods/t4_getpid.wasm \
	/mods/t9_multimod_app.wasm \
	/mods/t23_pthread.wasm)"

echo "${OUT}"

echo "${OUT}" | grep -q "t0_hello" \
	|| { echo "FAIL: missing t0_hello output" >&2; exit 1; }
echo "${OUT}" | grep -q "hello from vfs root" \
	|| { echo "FAIL: missing t1_read output (vfs root not shared/1:1)" >&2; exit 1; }
echo "${OUT}" | grep -qE "t0_hello\.wasm: exit=0" \
	|| { echo "FAIL: t0_hello did not exit 0" >&2; exit 1; }
echo "${OUT}" | grep -qE "t1_read\.wasm: exit=0" \
	|| { echo "FAIL: t1_read did not exit 0" >&2; exit 1; }

# t3_util_native — util/{size,log,arena}.h's wasi-style imports, resolved
# against the host's native registration (util/native.c). Asserts both
# directions of the app<->native address translation actually work (the
# "a[0]=0xab" line only appears if the host really wrote through into this
# guest's own linear memory) and that the global log floor set mid-mod
# really drops the INFO line while keeping the ERROR/raw ones.
echo "${OUT}" | grep -q "t3_util_native: size=88 MiB" \
	|| { echo "FAIL: size.h import wrong/missing" >&2; exit 1; }
echo "${OUT}" | grep -q "should not appear" \
	&& { echo "FAIL: log.h global floor did not drop a below-floor write" >&2; exit 1; }
echo "${OUT}" | grep -q "\[ERROR\] t3_util_native: at/above floor" \
	|| { echo "FAIL: log.h write() at/above floor missing" >&2; exit 1; }
echo "${OUT}" | grep -q "t3_util_native: raw, unfiltered" \
	|| { echo "FAIL: log.h write_raw() missing" >&2; exit 1; }
echo "${OUT}" | grep -q "t3_util_native: a\[0\]=0xab used=64" \
	|| { echo "FAIL: arena.h import did not alloc into this guest's own memory" >&2; exit 1; }
echo "${OUT}" | grep -q "t3_util_native: used_after_free=0" \
	|| { echo "FAIL: arena.h free() did not coalesce back to empty" >&2; exit 1; }
echo "${OUT}" | grep -qE "t3_util_native: lz4 [0-9]+ -> [0-9]+ bytes" \
	|| { echo "FAIL: lz4.h compress() import missing/failed" >&2; exit 1; }
echo "${OUT}" | grep -q "t3_util_native: lz4 round-trip ok" \
	|| { echo "FAIL: lz4.h decompress() did not round-trip back to the original bytes" >&2; exit 1; }
echo "${OUT}" | grep -qE "t3_util_native: tar wrote [0-9]+ bytes \(2 entries\)" \
	|| { echo "FAIL: tar.h writer import missing/failed" >&2; exit 1; }
echo "${OUT}" | grep -qE "t3_util_native: tar archive lz4 [0-9]+ -> [0-9]+ bytes" \
	|| { echo "FAIL: tar+lz4 archive compression missing/failed" >&2; exit 1; }
echo "${OUT}" | grep -q "t3_util_native: tar entry name=data/ size=0 is_dir=1" \
	|| { echo "FAIL: tar.h iter did not walk back the dir entry" >&2; exit 1; }
echo "${OUT}" | grep -qE "t3_util_native: tar entry name=data/quote\.txt size=[0-9]+ is_dir=0" \
	|| { echo "FAIL: tar.h iter did not walk back the file entry" >&2; exit 1; }
echo "${OUT}" | grep -q "t3_util_native: tar+lz4 round-trip ok" \
	|| { echo "FAIL: tar.h writer->lz4->tar.h iter round-trip did not match the original entries" >&2; exit 1; }
echo "${OUT}" | grep -qE "t3_util_native\.wasm: exit=0" \
	|| { echo "FAIL: t3_util_native did not exit 0" >&2; exit 1; }

# t4_getpid — runtime/process.h's spawn() auto-injects "PID=<n>"; app.c's
# scripted mode already routes every run through spawn()+wait() (see
# app.c), so this exercises the real end-to-end path, not a synthetic one.
echo "${OUT}" | grep -qE "t4_getpid: PID=[0-9]+" \
	|| { echo "FAIL: PID env var missing/malformed for t4_getpid" >&2; exit 1; }
echo "${OUT}" | grep -qE "t4_getpid\.wasm: exit=0" \
	|| { echo "FAIL: t4_getpid did not exit 0" >&2; exit 1; }

# t9_multimod_app -> t8_multimod_lib — WAMR's own WASM_ENABLE_MULTI_MODULE
# feature (runtime/runtime.c's module_reader), one .wasm importing a
# function straight from another .wasm's own .wasm bytes, no host
# round-trip for that one call. t8_multimod_lib.wasm itself is never named
# on pm-linux-runtime's own argv above — it only ever gets loaded because
# module_reader resolves it lazily, by name, while loading t9 itself.
echo "${OUT}" | grep -q "t9_multimod_app: t8_multimod_lib_add(3, 4) = 7" \
	|| { echo "FAIL: multi-module import (t9_multimod_app -> t8_multimod_lib) did not run" >&2; exit 1; }
echo "${OUT}" | grep -qE "t9_multimod_app\.wasm: exit=0" \
	|| { echo "FAIL: t9_multimod_app did not exit 0" >&2; exit 1; }

# t23_pthread — guest pthread_create()/join() against the default
# wasm32-wasip1-threads mod build (shared linear memory + wasi thread-spawn).
echo "${OUT}" | grep -q "t23_pthread: worker wrote 42" \
	|| { echo "FAIL: guest pthread_create/join did not share the worker write" >&2; exit 1; }
echo "${OUT}" | grep -qE "t23_pthread\.wasm: exit=0" \
	|| { echo "FAIL: t23_pthread did not exit 0" >&2; exit 1; }

echo "verify-linux: OK"
