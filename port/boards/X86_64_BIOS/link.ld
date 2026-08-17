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
    PROVIDE_HIDDEN (__start_pm_metal_externals = .);
    KEEP(*(pm_metal_externals))
    PROVIDE_HIDDEN (__stop_pm_metal_externals = .);
    . = ALIGN(8);
    PROVIDE_HIDDEN (__start_pm_mod_boot = .);
    KEEP(*(pm_mod_boot))
    PROVIDE_HIDDEN (__stop_pm_mod_boot = .);
    . = ALIGN(8);
    PROVIDE_HIDDEN (__start_pm_mod_bootdep = .);
    KEEP(*(pm_mod_bootdep))
    PROVIDE_HIDDEN (__stop_pm_mod_bootdep = .);
    . = ALIGN(8);
    PROVIDE_HIDDEN (__start_pm_metal_drv = .);
    KEEP(*(pm_metal_drv))
    PROVIDE_HIDDEN (__stop_pm_metal_drv = .);
    . = ALIGN(8);
    PROVIDE_HIDDEN (__start_pm_metal_class = .);
    KEEP(*(pm_metal_class))
    PROVIDE_HIDDEN (__stop_pm_metal_class = .);
  }

  .init_array : {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array))
    PROVIDE_HIDDEN (__init_array_end = .);
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
