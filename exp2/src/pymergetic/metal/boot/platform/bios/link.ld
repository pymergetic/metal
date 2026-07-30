/* exp2 BIOS Multiboot ELF64 — load at 16MiB (trampoline ELF32 embeds + copies). */
ENTRY(_start)

SECTIONS
{
  . = 16M;
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
