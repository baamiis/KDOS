/*
 * sam3x8e.ld — Linker script for Arduino Due bare-metal KTOS examples
 *
 * SAM3X8E memory map (Arduino Due application):
 *   Flash bank 0+1: 0x00080000, 512 KB  (SAM-BA bootloader loads apps here)
 *   SRAM bank 0:    0x20000000,  64 KB
 *   SRAM bank 1:    0x20080000,  32 KB  (unused — only bank 0 used here)
 *
 * Stack grows downward from the top of SRAM0.
 * Heap (for malloc) grows upward from the end of .bss.
 */

MEMORY {
    FLASH (rx)  : ORIGIN = 0x00080000, LENGTH = 512K
    SRAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}

/* Initial stack pointer = top of SRAM */
_estack = ORIGIN(SRAM) + LENGTH(SRAM);

SECTIONS {
    /* Vector table must be first in flash */
    .vectors : {
        KEEP(*(.vectors))
    } > FLASH

    .text : {
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    /* Load address of .data (in flash) */
    _sidata = LOADADDR(.data);

    .data : {
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > SRAM AT > FLASH

    .bss (NOLOAD) : {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > SRAM

    /* Heap starts after .bss — used by malloc() via nosys.specs sbrk */
    end  = _ebss;
    PROVIDE (_end = _ebss);
}
