SECTIONS {
.text 0x80076000 : { *(.text) }
.data 0x80076680 : { *(.data) }
.bss 0x800766c0 (NOLOAD) : { *(.bss) *(COMMON) }
.reginfo 0 : { *(.reginfo) }
}
