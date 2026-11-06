MEMORY { FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 1M  RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 256K  SDRAM (rw) : ORIGIN = 0x80000000, LENGTH = 1M }
SECTIONS {
  .text : { *(.text*) *(.rodata*) } > FLASH
  .data : { *(.data*) } > RAM
  .bss : { *(.bss*) *(COMMON) } > RAM
  .sdram (NOLOAD) : { *(.sdram*) } > SDRAM
  /DISCARD/ : { *(.ARM.exidx*) *(.init_array*) }
}
