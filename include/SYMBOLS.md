# Symbol names: C ↔ Rust ↔ Python (Metal)

Grouped by `include/pymergetic/metal/<path>`. Path == module.

Coverage % / async compliance / maintain hints:
[`../docs/MODULE_MATRIX.md`](../docs/MODULE_MATRIX.md).

Columns: **C** · **RS** · **Python** · **Seat** · **Status**

| Path | C | RS | Python | Seat | Status |
|------|---|----|--------|------|--------|
| `util.lz4` | `pm_metal_util_lz4_*` | callee `__init__.rs` | `pymergetic.metal.util.lz4` | all | ok |
| `util.size` | `pm_metal_util_size_*` | callee `__init__.rs` / C twin browser | `pymergetic.metal.util.size` | all | ok |
| `util.fourcc` | `pm_metal_util_fourcc_*` | face `__init__.rs` | `pymergetic.metal.util.fourcc` | all | ok |
| `util.eightcc` | `pm_metal_util_eightcc_*` | face `__init__.rs` | `pymergetic.metal.util.eightcc` | all | ok |
| `util.ascii` | `pm_metal_util_ascii_*` | face `__init__.rs` | `pymergetic.metal.util.ascii` | all | ok |
| `util.tar` | `pm_metal_util_tar_*` | callee `__init__.rs` (FW) / `tar_block.c` (browser) | `pymergetic.metal.util.tar` | all | ok |
| `util.endian` | `pm_metal_util_endian_*` (+ `*_inline`) | face `__init__.rs` | `pymergetic.metal.util.endian` | all | ok |
| `auth` | `pm_metal_auth_*` | face `__init__.rs` | `pymergetic.metal.auth` | all | ok |
| `trust` | `pm_metal_trust_*` | face `__init__.rs` | `pymergetic.metal.trust` | all | ok |
| `externals` | `pm_metal_external_*` | — | `pymergetic.metal.externals` | all | ok |
| `fs` | `pm_metal_fs_*` (+ `ops_*`) | callee `__init__.rs` / `ops.rs` | `pymergetic.metal.fs` | firmware | ok |
| `fs.embed` | `pm_metal_fs_embed_*` | callee `__init__.rs` | `pymergetic.metal.fs.embed` | firmware | ok |
| `fs.fat` | `pm_metal_fs_fat_*` | callee `__init__.rs` | `pymergetic.metal.fs.fat` | firmware | ok |
| `fs.littlefs` | `pm_metal_fs_littlefs_*` | callee `__init__.rs` | `pymergetic.metal.fs.littlefs` | firmware | ok |
| `fs.mtar` | `pm_metal_fs_mtar_*` | callee `__init__.rs` | `pymergetic.metal.fs.mtar` | firmware | ok |
| `fs.overlay` | `pm_metal_fs_overlay_*` | callee `__init__.rs` | `pymergetic.metal.fs.overlay` | firmware | ok |
| `fs.tmpfs` | `pm_metal_fs_tmpfs_*` | callee `__init__.rs` | `pymergetic.metal.fs.tmpfs` | firmware | ok |
| `fs.vfs` | `pm_metal_fs_vfs_*` | callee `__init__.rs` | `pymergetic.metal.fs.vfs` | firmware | ok |
| `fs.wasmmod` | `pm_metal_fs_wasmmod_*` | callee `__init__.rs` | `pymergetic.metal.fs.wasmmod` | firmware | ok |
| `fs.zip` | `pm_metal_fs_zip_*` | callee `__init__.rs` | `pymergetic.metal.fs.zip` | firmware | ok |
| `hwtree` | `pm_metal_hwtree_*` | callee `__init__.rs` | `pymergetic.metal.hwtree` | firmware | ok |
| `mem.arena` | `pm_metal_mem_arena_*` | callee `__init__.rs` / browser `abi_faces_link.c` | `pymergetic.metal.mem.arena` | all | ok |
| `mem.lock` | `pm_metal_mem_lock_{spin,mutex}_*` | `spin.rs` / `mutex.rs` / browser `abi_faces_link.c` | `pymergetic.metal.mem.lock` | all | ok |
| `mem.tlsf` | `pm_metal_mem_tlsf_*` | callee `__init__.rs` / browser `abi_faces_link.c` | `pymergetic.metal.mem.tlsf` | all | ok |
| `rt` | `pm_metal_rt_*` | callee `__init__.rs` / browser `rt_block.c` | `pymergetic.metal.rt` | all | ok |
| `wamr_host` | `pm_metal_wasm_*` | callee `__init__.rs` / `_fetch.rs` | `pymergetic.metal.wamr_host` | firmware | ok |
| `net.ip` | `pm_metal_net_ip_*` | face `__init__.rs` | `pymergetic.metal.net.ip` | firmware | ok |
| `net.wg` | `pm_metal_net_wg_*` | face `__init__.rs` | `pymergetic.metal.net.wg` | firmware | ok |
| `net.ssh` | `pm_metal_net_ssh_*` | face `__init__.rs` | `pymergetic.metal.net.ssh` | firmware | ok |
| `net.faces` | `pm_metal_net_face_*` | face `__init__.rs` | `pymergetic.metal.net.faces` | firmware | ok |
| `net.microdot` | `pm_metal_net_microdot_*` | face `__init__.rs` | `pymergetic.metal.net.microdot` | all | into-Py (C/RS 16 of API 20) |
| `inspect` | `pm_metal_inspect_*` (+ `py_*`) | face `__init__.rs` | `pymergetic.metal.inspect` | all | into-Py |
| `arch` | `pm_metal_arch_*` | face `__init__.rs` | `pymergetic.metal.arch` | all | CFG + into-Py |
| `arch.wasm` | `pm_metal_arch_wasm_*` | face `__init__.rs` | `pymergetic.metal.arch.wasm` | all | into-Py |
| `arch.x86` | `pm_metal_arch_x86_*` | face `__init__.rs` | `pymergetic.metal.arch.x86` | all | into-Py |
| `arch.x86_64` | `pm_metal_arch_x86_64_*` | face `__init__.rs` | `pymergetic.metal.arch.x86_64` | all | into-Py |
| `unix.x86` | `pm_metal_unix_x86_*` | face `__init__.rs` | `pymergetic.metal.unix.x86` | unix | into-Py |
| `unix.x86_64` | `pm_metal_unix_x86_64_*` | face `__init__.rs` | `pymergetic.metal.unix.x86_64` | unix | into-Py |
| `dev.serial` | `pm_metal_dev_serial_*` | face `__init__.rs` | `pymergetic.metal.dev.serial` | firmware | ok |

Missing seat ⇒ module **not nested** ⇒ `ImportError` (no stub `-1`).
