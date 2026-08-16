# extmod/metal — Metal product as a µPy extmod

metalpython = **upywm + Metal**. This directory is Metal’s µPy package
(`pymergetic.metal`). Fleet: [os-sdk `REPO_LAYOUT.md`](https://github.com/pymergetic/os-sdk/blob/main/packages/REPO_LAYOUT.md).
`metal.git` **`main`** is this card tree; **`preview`** is the standalone CMake runtime (metal-doom).
one defining lang, C/RS/Py faces — not a Python-only twin.

**Heap is wasmmod, not Metal:** `pymergetic.util.mem` (`impl = "c"`).
`#include "pymergetic/util/mem/__exports__.h"` / generated `.rs`. There is
**no** `pymergetic.metal.mem`. Dual-arena usage is two
`pm_util_mem_arena_create` calls.

```bash
make -C ports/unix MICROPY_PY_WASM=1 MICROPY_PY_METAL=1 BUILD=build-metal
make -C extmod/metal test
make -C extmod/metal upy
# unix boot: arena → runners → lo → metal io_ops → host_boot (GC stays on)
./ports/unix/build-metal/micropython -c "import pymergetic.metal as m; print(m.ready())"
```

`pymergetic.metal.async` is the stackless runner (ready ring + timer list +
`run_until`). `pymergetic.metal.net.ip` is IPv4 + ICMP + UDP + TCP + **lo**
(lo-reliable TCP on loopback; L2 TCP rexmit after `sim_drop`). Faces are
**`pymergetic.util.gen`** output (`PM_MOD_EXPORT_C` in `__impl__.c`).
Regenerate: `make -C extmod/metal gen` (wasmmod-gen also walks `../metal/src`
from the wasmmod crate). `pymergetic.metal.net.tls`
wraps the same µPy-vendored mbedtls on ip socks (`VERIFY_NONE` client).
`pymergetic.metal.net.http` is the GET client and strong `pm_metal_wasm_io_*`
(parked `io.fetch` to lo, `http://` and `https://`). `pymergetic.metal.net.http.asgi`
is the RS HTTP/1.0 server on the same socks (`impl = rs`). L2 NICs live in
`pymergetic.metal.drivers.net` (tap, virtio, bge, sim) — hardware adapters, not the
stack; gfx/audio land under `pymergetic.metal.drivers` later. `wg` stays
`pymergetic.metal.net.wg` (tunnel on ip, not a NIC). TCP rexmit lives in ip and
is pumped on L2. `pymergetic.metal.net.dns`
answers A queries on ip UDP. `ntp` is SNTP, `dhcp` is DISCOVER/OFFER then `if_up`,
`tftp` is RRQ/DATA, `ssh` sends an SSH-2.0 ident on TCP. Cards compile here only
(`metal.mk`). Host mbedtls objects are the same `lib/mbedtls` unix already
links — not a second copy in firmware.

Unix `MICROPY_PY_METAL=1` boots from `pm_metal_boot` (before `pm_wasmmod_host_boot`):
`util.mem` arena, async runner, ip+lo, tls, http, then `pm_wasmmod_metal_io_ops_init` + `io_set`.
`host_boot` keeps that table. Packet/`io.fetch` bytes stay on the arena, not the GC heap.
Do not auto-listen ASGI/L7 on unix boot.

Not this slice: virtio PCI MMIO, Noise_IK WireGuard, `register_upy`.
