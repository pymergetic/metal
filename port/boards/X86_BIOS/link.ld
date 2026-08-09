/* i686 BIOS Multiboot ELF32 — load at 1MiB (classic Multiboot). */
ENTRY(_start)

SECTIONS
{
  . = 1M;
  PROVIDE_HIDDEN (__pm_metal_image_base = .);

  .multiboot : {
    KEEP(*(.multiboot))
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
