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
| `util.tar` | `pm_metal_util_tar_*` | callee `__init__.rs` | `pymergetic.metal.util.tar` | firmware | ok |
| `util.endian` | `pm_metal_util_endian_*` (+ `*_inline`) | — | `pymergetic.metal.util.endian` | all | ok |
| `auth` | `pm_metal_auth_*` | face `__init__.rs` | `pymergetic.metal.auth` | all | ok |
| `trust` | `pm_metal_trust_*` | face `__init__.rs` | `pymergetic.metal.trust` | all | ok |
| `externals` | `pm_metal_external_*` | — | `pymergetic.metal.externals` | all | ok |
| `net.ip` | `pm_metal_net_ip_*` | FFI face | `pymergetic.metal.net.ip` | firmware | ok |
| `net.wg` | `pm_metal_net_wg_*` | FFI face | `pymergetic.metal.net.wg` | firmware | ok |
| `net.ssh` | `pm_metal_net_ssh_*` | face `__init__.rs` | `pymergetic.metal.net.ssh` | firmware | ok |
| `net.faces` | `pm_metal_net_face_*` | face `__init__.rs` | `pymergetic.metal.net.faces` | firmware | ok |
| `net.microdot` | `pm_metal_net_microdot_*` | face `__init__.rs` | `pymergetic.metal.net.microdot` | all | into-Py |
| `inspect` | `pm_metal_inspect_*` | face `__init__.rs` | `pymergetic.metal.inspect` | all | into-Py |
| `arch` | `pm_metal_arch_*` | face `__init__.rs` | `pymergetic.metal.arch` | all | CFG + into-Py |
| `dev.serial` | `pm_metal_dev_serial_*` | face `__init__.rs` | `pymergetic.metal.dev.serial` | firmware | ok |

Missing seat ⇒ module **not nested** ⇒ `ImportError` (no stub `-1`).
