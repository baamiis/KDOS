/*
 * samd21g18.ld — Linker script for Arduino Zero bare-metal KTOS examples
 *
 * ATSAMD21G18 memory map (with 8 KB Arduino/UF2 bootloader):
 *   Flash: 0x00000000, 256 KB — bootloader occupies 0x00000000–0x00001FFF
 *   Application starts at 0x00002000 (248 KB available)
 *   SRAM:  0x20000000, 32 KB
 *
 * Stack grows downward from the top of SRAM.
 */

MEMORY {
    FLASH (rx) : ORIGIN = 0x00002000, LENGTH = 248K
    RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 32K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS {
    .vectors : {
        KEEP(*(.vectors))
    } > FLASH

    .text : {
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    _sidata = LOADADDR(.data);

    .data : {
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    .bss (NOLOAD) : {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    end = _ebss;
    PROVIDE(_end = _ebss);
}
