/* exp2 BIOS Multiboot ELF64 — load at 64MiB (trampoline ELF32 embeds + copies).
 * 16MiB was too tight once mods.mtar stages large apps (e.g. doom IWAD). */
ENTRY(_start)

SECTIONS
{
  . = 64M;
  PROVIDE_HIDDEN (__pm_metal_image_base = .);

  .bootinfo : {
    KEEP(*(.bootinfo))
  }

  .multiboot2 : {
    KEEP(*(.multiboot2))
  }

  .text : {
    KEEP(*(.text.boot))
    /* Rust C ABI — survive --gc-sections when only force-undefined. */
    KEEP(*(.text.pm_metal_rt_*))
    KEEP(*(.text.pm_metal_fs_*))
    *(.text .text.*)
  }

  .rodata : {
    *(.rodata .rodata.*)
  }

  .data : {
    *(.data .data.*)
    . = ALIGN(16);
    PROVIDE_HIDDEN (__pm_metal_externals_start = .);
    KEEP (*(SORT_BY_NAME (.pm_metal_externals.*)));
    PROVIDE_HIDDEN (__pm_metal_externals_end = .);
    . = ALIGN(16);
    PROVIDE_HIDDEN (__pm_metal_seats_start = .);
    KEEP (*(SORT_BY_NAME (.pm_metal_seats.*)));
    PROVIDE_HIDDEN (__pm_metal_seats_end = .);
  }

  .bss : {
    *(.bss .bss.*)
    *(COMMON)
  }

  . = ALIGN(4096);
  PROVIDE_HIDDEN (__pm_metal_image_end = .);

  /DISCARD/ : {
    *(.note*)
    *(.comment*)
    *(.eh_frame*)
  }
}
