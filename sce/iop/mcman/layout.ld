SECTIONS
{
  .text : { *(.text) }
  .rodata : { *(.rodata) }
  .data : { *(.data) }
  .bss : { *(.bss) *(.bss.mcman) *(COMMON) }
}
