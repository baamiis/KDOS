/* Minimal linker script for STM32L031 (32KB flash, 8KB RAM) */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 32K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 8K
}

SECTIONS
{
    .text : { *(.text*) *(.rodata*) } > FLASH
    .data : { *(.data*) } > RAM AT > FLASH
    .bss  : { *(.bss*) *(COMMON) } > RAM
    _stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
