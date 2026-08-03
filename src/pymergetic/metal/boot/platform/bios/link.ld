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
    *(.text .text.*)
  }

  .rodata : {
    *(.rodata .rodata.*)
  }

  .data : {
    *(.data .data.*)
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
