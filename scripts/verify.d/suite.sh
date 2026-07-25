# Default verify suite contract — platform-agnostic.
# Port scripts source this, then run/collect output their own way, then call
# pm_suite_expect_* against OUT (stdout string) or a log file.
#
# Guest paths (symmetric package): /mods/tests/<name>.wasm, /mods/apps/<name>.wasm
# Kernel MicroPython is core (metal/py), not a guest package.
#
# Slices (every platform default must cover these where the port has support):
#   basic t0/t1 | utils t3 | pid t4 | multimod t9←t8 | pthread t23
#   crypto t31 | process+sockets
#   tmpfs / populate / proc   (where the port supports them)
# shellcheck shell=bash

# Core scripted set — basenames under /mods/tests/.
PM_SUITE_CORE_MODS=(
	t0_hello.wasm
	t1_read.wasm
	t3_util_native.wasm
	t4_getpid.wasm
	t8_multimod_lib.wasm
	t9_multimod_app.wasm
	t23_pthread.wasm
	t31_crypto.wasm
)


# Minimal hostdir VFS root. Guest content (mods, …) comes from
# embedded lz4 packages applied at boot (pkg_apply_all) — not hostdir install.
# (PM_SUITE_CORE_MODS is only the default *run* list, not the package set.)
pm_suite_stage_package() {
	local vfs_root="$1"
	mkdir -p "${vfs_root}"
}

# Alias — historical name; always full package.
pm_suite_stage_mods() {
	pm_suite_stage_package "$1"
}
# Expect scripted-core markers in OUT (text logs).
# $1 = path basename pattern for exit lines: "t0_hello" (always long names).
pm_suite_expect_scripted() {
	local t0b="${1:-t0_hello}" t1b="${2:-t1_read}" t3b="${3:-t3_util_native}"
	local t4b="${4:-t4_getpid}" t9b="${5:-t9_multimod_app}" t23b="${6:-t23_pthread}"
	local t31b="${7:-t31_crypto}"

	pm_expect - "t0_hello" "missing t0_hello output"
	pm_expect - "hello from vfs root" "missing t1_read output (vfs root not shared/1:1)"
	pm_expect_re - "${t0b}\\.wasm: exit=0" "t0 did not exit 0"
	pm_expect_re - "${t1b}\\.wasm: exit=0" "t1 did not exit 0"

	pm_expect - "t3_util_native: size=88 MiB" "size.h import wrong/missing"
	pm_expect_absent - "should not appear" "log.h global floor did not drop a below-floor write"
	pm_expect - "[ERROR] t3_util_native: at/above floor (expected)" "log.h write() at/above floor missing"
	pm_expect - "t3_util_native: raw, unfiltered" "log.h write_raw() missing"
	pm_expect - "t3_util_native: a[0]=0xab used=64" "arena.h import did not alloc into guest memory"
	pm_expect - "t3_util_native: used_after_free=0" "arena.h free() did not coalesce"
	pm_expect_re - "t3_util_native: lz4 [0-9]+ -> [0-9]+ bytes" "lz4.h compress() missing"
	pm_expect - "t3_util_native: lz4 round-trip ok" "lz4.h decompress() round-trip failed"
	pm_expect_re - "t3_util_native: tar wrote [0-9]+ bytes \\(2 entries\\)" "tar.h writer missing"
	pm_expect_re - "t3_util_native: tar archive lz4 [0-9]+ -> [0-9]+ bytes" "tar+lz4 missing"
	pm_expect - "t3_util_native: tar entry name=data/ size=0 is_dir=1" "tar.h dir entry missing"
	pm_expect_re - "t3_util_native: tar entry name=data/quote\\.txt size=[0-9]+ is_dir=0" "tar.h file entry missing"
	pm_expect - "t3_util_native: tar+lz4 round-trip ok" "tar+lz4 round-trip failed"
	pm_expect_re - "${t3b}\\.wasm: exit=0" "t3 did not exit 0"

	pm_expect_re - "t4_getpid: PID=[0-9]+" "PID env var missing/malformed"
	pm_expect_re - "${t4b}\\.wasm: exit=0" "t4 did not exit 0"

	pm_expect - "t9_multimod_app: t8_multimod_lib_add(3, 4) = 7" "multi-module import (t9→t8) did not run"
	pm_expect_re - "${t9b}\\.wasm: exit=0" "t9 did not exit 0"

	pm_expect - "t23_pthread: worker wrote 42" "guest pthread_create/join did not share worker write"
	pm_expect_re - "${t23b}\\.wasm: exit=0" "t23 did not exit 0"

	pm_expect - "t31_crypto: hash ok" "t31 hash"
	pm_expect - "t31_crypto: aead round-trip ok" "t31 aead"
	pm_expect_re - "${t31b}\\.wasm: exit=0" "t31 did not exit 0"
}

# Same as pm_suite_expect_scripted (historical alias for ports that skip t31).
pm_suite_expect_scripted_no_net() {
	local t0b="${1:-t0_hello}" t1b="${2:-t1_read}" t3b="${3:-t3_util_native}"
	local t4b="${4:-t4_getpid}" t9b="${5:-t9_multimod_app}" t23b="${6:-t23_pthread}"

	pm_expect - "t0_hello" "missing t0_hello output"
	pm_expect - "hello from vfs root" "missing t1_read output (vfs root not shared/1:1)"
	pm_expect_re - "${t0b}\\.wasm: exit=0" "t0 did not exit 0"
	pm_expect_re - "${t1b}\\.wasm: exit=0" "t1 did not exit 0"

	pm_expect - "t3_util_native: size=88 MiB" "size.h import wrong/missing"
	pm_expect_absent - "should not appear" "log.h global floor did not drop a below-floor write"
	pm_expect - "[ERROR] t3_util_native: at/above floor (expected)" "log.h write() at/above floor missing"
	pm_expect - "t3_util_native: raw, unfiltered" "log.h write_raw() missing"
	pm_expect - "t3_util_native: a[0]=0xab used=64" "arena.h import did not alloc into guest memory"
	pm_expect - "t3_util_native: used_after_free=0" "arena.h free() did not coalesce"
	pm_expect_re - "t3_util_native: lz4 [0-9]+ -> [0-9]+ bytes" "lz4.h compress() missing"
	pm_expect - "t3_util_native: lz4 round-trip ok" "lz4.h decompress() round-trip failed"
	pm_expect_re - "t3_util_native: tar wrote [0-9]+ bytes \\(2 entries\\)" "tar.h writer missing"
	pm_expect_re - "t3_util_native: tar archive lz4 [0-9]+ -> [0-9]+ bytes" "tar+lz4 missing"
	pm_expect - "t3_util_native: tar entry name=data/ size=0 is_dir=1" "tar.h dir entry missing"
	pm_expect_re - "t3_util_native: tar entry name=data/quote\\.txt size=[0-9]+ is_dir=0" "tar.h file entry missing"
	pm_expect - "t3_util_native: tar+lz4 round-trip ok" "tar+lz4 round-trip failed"
	pm_expect_re - "${t3b}\\.wasm: exit=0" "t3 did not exit 0"

	pm_expect_re - "t4_getpid: PID=[0-9]+" "PID env var missing/malformed"
	pm_expect_re - "${t4b}\\.wasm: exit=0" "t4 did not exit 0"

	pm_expect - "t9_multimod_app: t8_multimod_lib_add(3, 4) = 7" "multi-module import (t9→t8) did not run"
	pm_expect_re - "${t9b}\\.wasm: exit=0" "t9 did not exit 0"

	pm_expect - "t23_pthread: worker wrote 42" "guest pthread_create/join did not share worker write"
	pm_expect_re - "${t23b}\\.wasm: exit=0" "t23 did not exit 0"
}

