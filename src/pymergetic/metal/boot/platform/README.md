# Legacy forge platform (not an entry)

Firmware **entry** is `port/boards/X86_64_{BIOS,UEFI}/main.c` only.
Shared live boot is `port/boot/boot.c` + `port/hal/*`.

This tree remains for older forge bringup helpers; do not add a second `main`.
