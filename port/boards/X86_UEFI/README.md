# X86_UEFI (metal)

i686 / IA32 UEFI seat (`BOOTIA32.EFI`). Minimal EFI application (`UefiMain` → COM1).

```bash
make -C ports/metal BOARD=X86_UEFI ENGINE=mp LINK_WAMR=0
make -C ports/metal BOARD=X86_UEFI run
# OVMF: use an IA32 firmware image for QEMU bring-up
```
