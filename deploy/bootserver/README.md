# Boot server drop (iPXE + OpenWrt)

The **boot server** only needs a small iPXE NBP + a script. The **MetalPython
image** and guest packs live on **metal-cdn**.

## Master vs lab / own realm

| Role | Default | Override |
|------|---------|----------|
| **Master** kernel home + `metal.ipxe` CDN | `https://cdn.pymergetic.com/cdn` | — |
| Lab publish (`dev-up` docker) | `http://127.0.0.1:8000/cdn` | `METAL_CDN_URL` |
| Lab / own-realm *seat* bake | (same as master unless you say so) | `METAL_CDN_URL=…` at `make` |
| PXE script CDN | official realm | `METAL_PXE_CDN_URL=https://your.cdn/cdn` |
| Site add/replace at runtime | — | DHCP option **224** |

Building the product (“master”) always bakes the official realm. Local docker
and private CDNs are for bring-up or someone else’s realm — set the knobs;
don’t change the master default.

```text
DHCP → undionly.kpxe / ipxe.efi   (TFTP — tiny iPXE NBP only)
     → metal.ipxe                 (TFTP — config script only)
     → kernel/chain CDN lead      (HTTP — real MetalPython image)
     → metal boot → CDN autoexec → import …
```

**Boot server never hosts the product image.** Only iPXE + `metal.ipxe`.
Publish firmware to the PXE-reachable CDN (`METAL_PXE_CDN_URL`, default
official realm). Local docker `127.0.0.1:8000` is for the UI/build host, not PXE.

### HTTPS + iPXE

CDN edge must use an **RSA** Let’s Encrypt cert (`certbot --key-type rsa`) and
iPXE-friendly ciphers — see metal-cdn `deploy/nginx/cdn.conf` / `docs/PROXY.md`.
ECDSA (YE2) certs break iPXE TLS.

First-stage NBPs must be a **current** iPXE with HTTPS (Ubuntu 2022 ROM is not enough):

```bash
./extmod/metal/deploy/bootserver/build-nbp.sh
# push undionly.kpxe + ipxe.efi to the TFTP root (upload-bootserver --nbp …)
```

### QEMU smoke (no router)

```bash
./extmod/metal/deploy/bootserver/build-nbp.sh   # once
./extmod/metal/deploy/bootserver/qemu-pxe.sh              # HTTPS official CDN
./extmod/metal/deploy/bootserver/qemu-pxe.sh --local      # HTTP host docker
./extmod/metal/deploy/bootserver/qemu-pxe.sh --uefi
```

## What goes where

| File | What it is |
|------|------------|
| `metal.ipxe` | **Config** — iPXE script (`kernel`/`chain` → CDN lead) |
| `undionly.kpxe` | BIOS first-stage NBP (tiny binary) |
| `ipxe.efi` | UEFI first-stage NBP |
| CDN `…/artifacts/lead/pymergetic.metal.arch.*.{elf,efi}` | Real image (**not** on TFTP) |

## Push from a checkout

**One-shot** (build BIOS+UEFI product images → publish CDN lead → upload PXE config),
from the metal-cdn tree:

```bash
# convenience — everything including freestanding + bootserver
../metal-cdn/scripts/dev-up.sh --full

# or compose steps (speed vs convenience)
../metal-cdn/scripts/dev-up.sh --firmware --bootserver   # after CDN already up
../metal-cdn/scripts/dev-up.sh --firmware-only            # build+publish only
../metal-cdn/scripts/dev-up.sh --bootserver-only          # metal.ipxe / NBPs only
# After CDN client/layout changes: rebuild the image first
#   ../metal-cdn/scripts/dev-up.sh --no-upy --no-seed
```

That publishes arch seat packs on the CDN shelf:

| Package | Artifacts | Role |
|---------|-----------|------|
| `pymergetic.metal.arch.x86_64` | `.elf` + `.efi` | Freestanding x86_64 BIOS/UEFI |
| `pymergetic.metal.arch.x86` | `.elf` + `.efi` | Freestanding i686 BIOS/UEFI (`BOOTIA32`) |
| `pymergetic.metal.arch.wasm` | `.mjs` + `.wasm` | Browser seat; CDN UI `mp` engine |

iPXE: long mode → `x86_64`; plain i386 → `x86`.

| Artifact | Role |
|----------|------|
| `…/pymergetic.metal.arch.x86_64.elf` | BIOS ELF32 trampoline (signed) |
| `…/pymergetic.metal.arch.x86_64.efi` | UEFI PE (unsigned) |
| `…/pymergetic.metal.arch.wasm.{mjs,wasm}` | Emscripten loader + module |

**Arch gating:** DHCP option **93** picks the first-stage NBP (`undionly.kpxe` vs
`ipxe.efi`). Then `metal.ipxe` selects `pymergetic.metal.arch.${arch}.{elf,efi}`
from `${platform}` / `${arch}` (`cpuid --ext 29` promotes `i386` → `x86_64`).

**Bootserver only** (CDN image already published):

```bash
export METAL_PXE_HOST=192.168.10.1          # or 172.30.0.254 over WG
export METAL_PXE_PATH=/storage/tftp
# Must be reachable from the PXE client — NOT http://127.0.0.1:8000/cdn
export METAL_PXE_CDN_URL=https://cdn.pymergetic.com/cdn
export METAL_BOOT_IMAGE_URL=${METAL_PXE_CDN_URL}/artifacts/lead/pymergetic.metal.arch.x86_64.elf

# Always uploads metal.ipxe. If undionly.kpxe / ipxe.efi are missing on the
# server, downloads them from http://boot.ipxe.org/ and uploads.
./extmod/metal/deploy/upload-bootserver

# Config only (leave existing NBPs alone)
./extmod/metal/deploy/upload-bootserver --skip-nbp

# Do NOT park product images on TFTP for normal use — publish to CDN instead.
# (--image is an emergency CDN-miss fallback only)
```

OpenWrt often has **no rsync/sftp** — the script uses `scp -O` like the old
forge `upload-pxe`.

## DHCP

See [`dnsmasq.metal.conf.example`](dnsmasq.metal.conf.example) for the
`undionly` / `ipxe.efi` → `metal.ipxe` tag split and optional option **224**.

## Layout on the router

| File | Role |
|------|------|
| `undionly.kpxe` / `ipxe.efi` | First-stage NBP (upload once) |
| `metal.ipxe` | Chainload script (CDN URL baked in) |
| *(no metal.elf / metal.efi)* | Product image is on CDN |
