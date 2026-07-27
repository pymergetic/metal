/* Metal BIOS Multiboot2 ELF32 — load at 1MiB. */
ENTRY(_start)

PHDRS
{
  text   PT_LOAD FLAGS(5); /* PF_R | PF_X */
  rodata PT_LOAD FLAGS(4); /* PF_R */
  data   PT_LOAD FLAGS(6); /* PF_R | PF_W */
}

SECTIONS
{
  . = 1M;

  .multiboot : {
    KEEP(*(.multiboot))
  } :text

  .multiboot2 : {
    KEEP(*(.multiboot2))
  } :text

  .text : {
    KEEP(*(.text.boot))
    *(.text .text.*)
  } :text

  . = ALIGN(4096);

  .rodata : {
    *(.rodata .rodata.*)
  } :rodata

  . = ALIGN(4096);

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
  } :data

  .bss : {
    *(.bss .bss.*)
    *(COMMON)
  } :data

  /DISCARD/ : {
    *(.note*)
    *(.comment*)
    *(.eh_frame*)
  }
}
