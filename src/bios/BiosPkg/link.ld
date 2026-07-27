/* Metal BIOS Multiboot ELF64 — load at 8MiB (trampoline ELF32 + embed at 1MiB+). */
ENTRY(_start)

SECTIONS
{
  . = 8M;

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
    . = ALIGN(16);
    PROVIDE_HIDDEN (__pm_metal_shell_cmds_start = .);
    KEEP (*(SORT_BY_NAME (.pm_metal_shell_cmds.*)));
    PROVIDE_HIDDEN (__pm_metal_shell_cmds_end = .);
    . = ALIGN(16);
    PROVIDE_HIDDEN (__pm_metal_py_binds_start = .);
    KEEP (*(SORT_BY_NAME (.pm_metal_py_binds.*)));
    PROVIDE_HIDDEN (__pm_metal_py_binds_end = .);
    . = ALIGN(16);
    PROVIDE_HIDDEN (__pm_metal_keyb_layouts_start = .);
    KEEP (*(SORT_BY_NAME (.pm_metal_keyb_layouts.*)));
    PROVIDE_HIDDEN (__pm_metal_keyb_layouts_end = .);
    . = ALIGN(16);
    PROVIDE_HIDDEN (__pm_metal_externals_start = .);
    KEEP (*(SORT_BY_NAME (.pm_metal_externals.*)));
    PROVIDE_HIDDEN (__pm_metal_externals_end = .);
  }

  .bss : {
    *(.bss .bss.*)
    *(COMMON)
  }

  /DISCARD/ : {
    *(.note*)
    *(.comment*)
    *(.eh_frame*)
  }
}
