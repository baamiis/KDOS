/*
 * ktos_esp8266.ld — Minimal linker script for KTOS ESP8266 hello world.
 *
 * Produces the same IRAM/IROM split as eagle.app.v6.ld so that
 * "esptool.py elf2image" generates the correct 0x00000 and 0x10000 binaries,
 * but without the SDK library dependencies that cause linker segfaults.
 *
 * Memory map (NodeMCU / Wemos D1 Mini — 4 MB flash):
 *   IRAM  0x40100000  32 KB  — time-critical code, interrupt handlers
 *   IROM  0x40210000  ~370KB — bulk of application code (execute-in-place)
 *   DRAM  0x3FFE8000  80 KB  — data, BSS, stack, heap
 */

MEMORY
{
    dram0_0_seg : org = 0x3FFE8000, len = 0x14000
    iram1_0_seg : org = 0x40100000, len = 0x8000
}

ENTRY(call_user_start)

INCLUDE eagle.rom.addr.v6.ld

SECTIONS
{
    /* ---- IRAM: all code loaded into RAM at boot ---------------------------
     * The hello_ktos demo is small enough (~1.3 KB) to fit entirely in IRAM.
     * This avoids requiring SPI flash-cache setup before the first IROM fetch.
     * IROM (XIP) support can be added later by calling Cache_Read_Enable()
     * from call_user_start before any access to 0x40210000+.
     * ----------------------------------------------------------------------- */
    .text : ALIGN(4)
    {
        _text_start = ABSOLUTE(.);
        *(.iram0.literal .iram0.text)
        *(.entry.text)
        *(.init.literal)
        *(.init)
        *(.irom0.literal .irom0.text)
        *(.literal .text .literal.* .text.*)
        _text_end = ABSOLUTE(.);
    } >iram1_0_seg

    /* ---- Data (initialised) ----------------------------------------------- */
    .data : ALIGN(4)
    {
        _data_start = ABSOLUTE(.);
        *(.data .data.*)
        *(.rodata .rodata.*)
        _data_end = ABSOLUTE(.);
    } >dram0_0_seg

    /* ---- BSS (zero-initialised) ------------------------------------------- */
    .bss (NOLOAD) : ALIGN(4)
    {
        _bss_start = ABSOLUTE(.);
        *(.bss .bss.*)
        *(COMMON)
        _bss_end = ABSOLUTE(.);
    } >dram0_0_seg
}
