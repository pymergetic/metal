#!/usr/bin/env bash
# Populate build/pxe/ (iPXE + staging). Kernel stays build/i386_bios/metal.elf —
# this drop does not own a separate ELF product.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/ext-apps.sh"
ELF="${1:-${ROOT}/build/i386_bios/metal.elf}"
PXE="${ROOT}/build/pxe"

if [[ ! -f "${ELF}" ]]; then
	echo "pxe: missing ${ELF} (build bios i386 first)" >&2
	exit 1
fi

rm -rf "${PXE}"
mkdir -p "${PXE}/boot/grub"

# Same package layout as EFI ESP — HTTP :8080 mirrors TFTP root.
pm_metal_ext_apps_stage_into "${PXE}"
# Same curated py/httpd/api stage as scripts/lib/efi-qemu.sh.
# shellcheck disable=SC1091
source "${ROOT}/scripts/lib/stage-py-httpd.sh"
pm_metal_stage_py_httpd_into "${PXE}"
if [[ -d "${ROOT}/mods/etc" ]]; then
	mkdir -p "${PXE}/etc"
	cp -a "${ROOT}/mods/etc/." "${PXE}/etc/"
fi
# Optional override of the checked-in lab Dropbear hostkey.
if [[ -n "${METAL_SSHD_HOSTKEY:-}" ]]; then
	if [[ ! -f "${METAL_SSHD_HOSTKEY}" ]]; then
		echo "pxe: METAL_SSHD_HOSTKEY not a file: ${METAL_SSHD_HOSTKEY}" >&2
		exit 1
	fi
	mkdir -p "${PXE}/etc/ssh"
	cp -f "${METAL_SSHD_HOSTKEY}" "${PXE}/etc/ssh/dropbear_ed25519_host_key"
fi
if [[ -d "${ROOT}/mods/www" ]]; then
	mkdir -p "${PXE}/mods/www"
	cp -a "${ROOT}/mods/www/." "${PXE}/mods/www/"
fi

# iPXE script — DHCP filename must be undionly.kpxe (small), NOT metal.elf.
# metal.elf (from i386_bios) is large and will fail with
# "NBP is too big for base memory". HTTP on :8080 already indexes /storage/tftp
# on this OpenWrt (same files as TFTP). Prefer HTTP for the large ELF; first-stage
# undionly still comes via TFTP/DHCP.
cat >"${PXE}/metal.ipxe" <<'EOF'
#!ipxe
echo Metal i386 via iPXE+HTTP
isset ${next-server} || set next-server 192.168.10.1
kernel http://${next-server}:8080/metal.elf || kernel metal.elf
boot || shell
EOF

cat >"${PXE}/boot/grub/grub.cfg" <<'EOF'
set timeout=2
set default=0
menuentry "pymergetic metal (i386)" {
	set gfxpayload=1024x768x32
	multiboot2 /metal.elf
	boot
}
EOF

cat >"${PXE}/README-PXE.md" <<'EOF'
# Metal i386 PXE drop

Staging only (`build/pxe/`). The Multiboot2 kernel is
`build/i386_bios/metal.elf` — upload scripts place it next to `metal.ipxe`
on the TFTP/HTTP root.

**Do not set DHCP boot filename to `metal.elf`.**  
PXE NBPs load into ~640KiB base memory; `metal.elf` is several MiB →
"NBP is too big for base memory".

## Recommended: OpenWrt + iPXE

TFTP root files:

- `undionly.kpxe` (or your usual iPXE NBP — small)
- `metal.ipxe` (this drop)
- `metal.elf` (from `build/i386_bios/`, uploaded beside this drop)
- optional: `mods/apps/<name>/...` for any external app staged via
  `METAL_EXT_APPS=<name>=<dir> ./scripts/build bios i386` (e.g. `packages/metal-doom`)
  (BIOS seeds these into the ESP RAM cache via HTTP :8080 after DHCP)

dnsmasq / luci DHCP:

- boot filename = `undionly.kpxe`  (first stage only)
- next-server = this TFTP host

Make iPXE run the script (pick one):

1. **Embedded script** in your iPXE build, or  
2. DHCP option 175 / `dhcp-option` that points iPXE at the script, or  
3. Chain from a tiny default: if you use `undionly.kpxe` with
   `dhcp-boot=metal.ipxe` **only for iPXE clients** (see dnsmasq
   `dhcp-match=set:ipxe,175` pattern), e.g.:

   ```
   dhcp-match=set:ipxe,175
   dhcp-boot=tag:!ipxe,undionly.kpxe
   dhcp-boot=tag:ipxe,metal.ipxe
   ```

`metal.ipxe` does: `kernel metal.elf` then `boot`.

If iPXE **loops**, filename is probably still `undionly.kpxe` on every
request (including after iPXE is running) — use the tag split above so the
second DHCP gets `metal.ipxe`, not iPXE again.

## Alternative: GRUB i386-pc

```bash
grub-mknetdir --net-directory=build/pxe --subdir=boot/grub \
  -d /usr/lib/grub/i386-pc
```

DHCP filename = `boot/grub/i386-pc/core.0` (not `metal.elf`).

Serial: COM1 115200.
EOF

echo "pxe: ok → ${PXE} (kernel: ${ELF})"
ls -la "${PXE}/metal.ipxe" "${PXE}/boot/grub/grub.cfg"
ls -la "${ELF}"
