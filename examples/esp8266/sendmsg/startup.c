/**
 * @file startup.c
 * @brief ESP8266 windowed-ABI entry point for KTOS.
 *
 * This file is intentionally compiled WITHOUT -mabi=call0 (windowed ABI)
 * so the ROM/SDK can call user_init() normally via CALL4.
 *
 * Jobs:
 *  1. Zero BSS (call_user_start, called by ROM before SDK init).
 *  2. Cross the ABI boundary into the CALL0 KTOS application via callx0.
 *
 * Compile flag: NO -mabi=call0 (default windowed ABI).
 */

#include <stdint.h>

/* =========================================================================
 * call_user_start — firmware entry point called by the ROM after boot.
 *
 * The ROM loads our IRAM segment and jumps here.  We must zero BSS then
 * call user_init().  Must live in IRAM so it is reachable before the IROM
 * flash mapping is enabled.
 * ========================================================================= */

extern uint32_t _bss_start;
extern uint32_t _bss_end;

void user_init(void);  /* forward declaration */

__attribute__((section(".iram0.text")))
void call_user_start(void)
{
    uint32_t *p = &_bss_start;
    while (p < &_bss_end) *p++ = 0;
    user_init();
}

/* =========================================================================
 * SDK entry point
 * ========================================================================= */

/* ktos_app_main lives in main.c, compiled with -mabi=call0 */
extern void ktos_app_main(void);

/**
 * @brief ESP8266 Non-OS SDK entry point (windowed ABI).
 *
 * Called by call_user_start after BSS is zeroed.
 * Crosses into CALL0 via callx0; a1 (SP) is preserved so ktos_app_main
 * inherits the valid stack pointer set up by our own ENTRY.
 *
 * The hello_ktos demo uses a polled CCOUNT delay and does not need the
 * FRC1 timer ISR, so no ets_isr_attach() call is made here.  A real
 * application that needs ktos_Sleep() must register the ISR (windowed ABI
 * call to ets_isr_attach) before the callx0.
 */
void user_init(void)
{
    __asm__ volatile (
        "callx0 %0"
        :: "r"(ktos_app_main)
        : "a0", "memory"
    );
    while (1);  /* never reached */
}
