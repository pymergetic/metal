# Metal — follow-up TODO

Captured **2026-07-22** after today's session (DT/blk, i386 PXE, net multi-if, shell registry, input lag, wasm FS, etc.).

---

## Shipped today (reference)

| Area | What landed |
|------|-------------|
| **DT / storage** | Multi-node inventory; virtio-blk + IDE PIO detect/bind; boot tree lists all nodes |
| **BIOS / generic PC** | Native **i386** Multiboot2 build + **PXE drop** (`build/bios/pxe/`) |
| **Net** | Multi-if (`eth0`…), per-if DHCP/DHCPv4, **bge** L2, `net set …` shell, bind-to-interface |
| **DHCPv6** | Stateless + **Metal stateful client** (lwIP `dhcp6_enable_stateful()` still stub) |
| **FS / wasm** | Host pointer parity in `fs.h`; doom `PM_METAL_FS_IO_PTR` fix |
| **Input** | Tab-based focus (shell vs guest); lag fix (partial present + 1 ms pump); guest `poll_key` |
| **Shell** | Registration pattern; commands live in topic modules (`net_shell.c`, etc.) |
| **Cleanup** | Core doom/game cruft removed; unused `string.h` in dhcp6 module |

---

## Must verify on real hardware (PXE / ThinkPad-class)

- [ ] PXE boot i386 image end-to-end (`./scripts/upload-pxe` → target machine)
- [ ] Shell typing feels snappy (lag fix built for BIOS; confirm on hardware)
- [ ] IDE disk visible + usable from shell / guest FS
- [ ] Wired **bge**: `net list`, `net set eth0 dhcp`, ping/DNS
- [ ] **Stateful DHCPv6** on a network with RA **M-flag** (lab network)

---

## Hardware gaps (platform-agnostic)

- [ ] **Real framebuffer** — still Multiboot tag or QEMU Bochs; no 855GM/VESA (or generic VESA) path
- [ ] **Real audio** — virtio-only; null on bare metal
- [ ] **More NICs** — virtio + bge today; no broad PCI net class scan / detector model like blk

---

## Platform / code health

- [ ] **Single source tree** — `src/bios/…` vs `src/efi/…` still duplicated; behavior can drift
- [ ] **EFI build smoke** after latest shell/net refactors (BIOS i386 verified; re-run EFI)
- [ ] **Docs** — update `IO.md`, `LIBC_ASYNC.md` for: bind-if, stateful dhcp6, shell registry, guest `poll_key`

---

## Nice-to-have / polish

- [ ] Shell: linker-section auto-register (drop manual `pm_metal_shell_cmds_register_*()` list in `shell_cmd.c`)
- [ ] Move `test` shell command next to boot code (`shell_core_cmds.c` → boot module)
- [ ] Raise or document **32-command** shell registry cap (`PM_METAL_SHELL_CMD_MAX`)
- [ ] Upstream or delete lwIP `dhcp6_enable_stateful()` stub once Metal client is proven on hardware

---

## Bottom line

Core app loop (tab → guest focus, wasm FS, faster shell input, i386 PXE, IDE, bge net, shell registry) is in place. **Not fully app-ready on arbitrary bare metal** until real FB, broader device detection, and on-hardware smoke (PXE + net + typing) are green.
