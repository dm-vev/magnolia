ENTRY(app_main)

PHDRS
{
  text PT_LOAD FILEHDR PHDRS FLAGS(5);  /* R + X */
  ro   PT_LOAD FLAGS(4);                /* R */
  data PT_LOAD FLAGS(6);                /* R + W */
}

SECTIONS
{
  . = 0;

  .hash : { *(.hash) } :ro
  .dynsym : { *(.dynsym) } :ro
  .dynstr : { *(.dynstr) } :ro
  .rela.dyn : { *(.rela.dyn) } :ro

  .text : ALIGN(4)
  {
    *(.literal .literal.*)
    *(.text .text.*)
  } :text

  .rodata : ALIGN(4) { *(.rodata .rodata.*) } :ro

  .got : ALIGN(4) { *(.got .got.*) } :data
  .data : ALIGN(4) { *(.data .data.*) } :data
  .bss : ALIGN(4) { *(.bss .bss.* COMMON) } :data

  /DISCARD/ :
  {
    *(.comment .note* .eh_frame* .gnu* .xtensa.info .xt.lit .xt.prop)
  }
}
