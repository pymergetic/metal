/* ELF32 Multiboot trampoline at 1MiB; embeds metal.elf and copies it to 64MiB. */
ENTRY(_start)

PHDRS
{
  text PT_LOAD FLAGS(5); /* PF_R | PF_X */
  data PT_LOAD FLAGS(6); /* PF_R | PF_W */
}

SECTIONS
{
  . = 1M;

  .multiboot : {
    KEEP(*(.multiboot))
  } :text

  .text : {
    *(.text .text.*)
  } :text

  . = ALIGN(4096);

  .data : {
    *(.data .data.*)
    *(.rodata .rodata.*)
  } :data

  .bss : {
    *(.bss .bss.*)
    *(COMMON)
  } :data

  ASSERT(. <= 0x4000000, "trampoline image overlaps Metal load address (64MiB)")
  ASSERT(_binary_metal_elf_end <= 0x4000000, "embedded metal.elf extends into 64MiB load window")

  /DISCARD/ : {
    *(.note*)
    *(.comment*)
    *(.eh_frame*)
  }
}
