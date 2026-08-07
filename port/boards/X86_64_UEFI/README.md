# X86_64_UEFI (metalmod)

Minimal EFI application (`UefiMain` → ConOut banner). No forge.

```bash
make -C ports/metal BOARD=X86_64_UEFI
make -C ports/metal BOARD=X86_64_UEFI run
# OVMF default: /usr/share/ovmf/OVMF.fd
```
