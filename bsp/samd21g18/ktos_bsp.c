/*
 * KTOS — Tiny Cooperative Task Switcher
 * Copyright (C) 2004-2025 Khalid Hamdou / BAAMIIS LIMITED
 * All rights reserved.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/**
 * @file ktos_bsp.c
 * @brief KTOS Board Support Package — Atmel ATSAMD21G18 (ARM Cortex-M0+)
 *
 * Supported boards: Arduino Zero.
 *
 * | Property  | Value                         |
 * |-----------|-------------------------------|
 * | Core      | ARM Cortex-M0+                |
 * | RAM       | 32 KB                         |
 * | Flash     | 256 KB                        |
 * | Clock     | 48 MHz (DFLL48M)              |
 * | Toolchain | arm-none-eabi-gcc             |
 *
 * Tick source: TC3 (IRQ 18 in peripheral table, NVIC bit 18).
 * TC3 is clocked from GCLK0 (48 MHz), MFRQ mode, CC0=47999 → 1 ms.
 *
 * Cortex-M0+ context switch differs from M3: push/pop of high registers
 * (R8-R11) must be done indirectly via R4-R7.
 *
 * ### TC3 ISR wiring
 * Add this to your application:
 * @code
 * void TC3_Handler(void) {
 *     *(volatile uint8_t *)0x42002C0EUL = 1;  // clear INTFLAG.OVF
 *     ktos_timer_irq_handler();
 * }
 * @endcode
 */

#include "../../core/ktos_hal.h"
#include <stdint.h>

/* =========================================================================
 * Interrupt control — Cortex-M0+
 * ========================================================================= */

void ktos_hal_DisableInterrupts(void)
{
    __asm volatile ("cpsid i" ::: "memory");
}

void ktos_hal_EnableInterrupts(void)
{
    __asm volatile ("cpsie i" ::: "memory");
}

/* =========================================================================
 * System timer — TC3, 1 ms tick at 48 MHz
 *
 * Clock path: GCLK0 (48 MHz) → PM APBCMASK TC3 → TC3 MFRQ, DIV1.
 * CC0 = 47999 → (47999+1) / 48000000 = 1 ms.
 *
 * TC3 NVIC IRQ = 18 (peripheral IRQ offset in SAMD21G18).
 * GCLK peripheral ID for TC3 = 18 (shared with TCC2 → GCLK ID 18).
 * ========================================================================= */
#define PM_APBCMASK     (*(volatile uint32_t *)0x40000420UL)
#define GCLK_CLKCTRL    (*(volatile uint16_t *)0x40000C02UL)
#define GCLK_STATUS     (*(volatile uint8_t  *)0x40000C01UL)

#define TC3_CTRLA       (*(volatile uint16_t *)0x42002C00UL)
#define TC3_CTRLBSET    (*(volatile uint8_t  *)0x42002C05UL)
#define TC3_INTENCLR    (*(volatile uint8_t  *)0x42002C0CUL)
#define TC3_INTENSET    (*(volatile uint8_t  *)0x42002C0DUL)
#define TC3_INTFLAG     (*(volatile uint8_t  *)0x42002C0EUL)
#define TC3_STATUS      (*(volatile uint8_t  *)0x42002C0FUL)
#define TC3_CC0         (*(volatile uint16_t *)0x42002C18UL)

#define NVIC_ISER0      (*(volatile uint32_t *)0xE000E100UL)

void ktos_hal_InitSystemTimer(void (*timer_isr_addr)(void))
{
    /* Enable TC3 APB clock (APBCMASK bit 11) */
    PM_APBCMASK |= (1u << 11);

    /* Connect GCLK0 (48 MHz) to TC3 (GCLK peripheral ID 18 = TCC2/TC3) */
    GCLK_CLKCTRL = (uint16_t)(18u | (1u << 14)); /* ID=18|GEN=0|CLKEN */
    while (GCLK_STATUS & (1u << 7)) {}

    /* Software reset TC3 */
    TC3_CTRLA = (1u << 0);  /* SWRST */
    while (TC3_STATUS & (1u << 7)) {}  /* wait SYNCBUSY */
    while (TC3_CTRLA & (1u << 0)) {}   /* wait SWRST self-clear */

    /* 16-bit MFRQ mode, no prescaler (DIV1) */
    TC3_CTRLA = (1u << 8); /* WAVEGEN=MFRQ; MODE=16-bit, PRESCALER=DIV1 are zero (default) */
    while (TC3_STATUS & (1u << 7)) {}

    /* CC0 = 47999 → 1 ms at 48 MHz */
    TC3_CC0 = 47999u;
    while (TC3_STATUS & (1u << 7)) {}

    /* Enable OVF (overflow) interrupt */
    TC3_INTENCLR = 0xFF;
    TC3_INTENSET = (1u << 0);  /* OVF */

    /* Enable TC3 IRQ in NVIC (IRQ 18) */
    NVIC_ISER0 = (1u << 18);

    /* Enable TC3 */
    TC3_CTRLA |= (1u << 1);  /* ENABLE */
    while (TC3_STATUS & (1u << 7)) {}

    (void)timer_isr_addr;
}

/* =========================================================================
 * Task stack initialisation — Cortex-M0+
 *
 * Stack layout (SP at lowest address after init):
 *   [R8][R9][R10][R11]   ← software-saved, popped first into R4-R7 then moved
 *   [R4][R5][R6 ][R7 ]   ← software-saved, popped directly
 *   [R0][R1][R2 ][R3 ]   ← hardware exception frame (task args)
 *   [R12][LR][PC][xPSR]  ← hardware exception frame
 * ========================================================================= */
void *ktos_hal_InitTaskStack(void *p_stack_base,
                              unsigned int stack_size_bytes,
                              WORD (*task_func_addr)(WORD, WORD, LONG),
                              void (*task_exit_handler_addr)(WORD),
                              WORD initial_msg_type,
                              WORD initial_sparam,
                              LONG initial_lparam)
{
    uint32_t *sp = (uint32_t *)((uint8_t *)p_stack_base + stack_size_bytes);
    sp = (uint32_t *)((uint32_t)sp & ~0x7UL);

    /* Hardware exception frame (high address → low) */
    *--sp = 0x01000000UL;                               /* xPSR — Thumb bit */
    *--sp = (uint32_t)task_func_addr;                   /* PC */
    *--sp = (uint32_t)task_exit_handler_addr;            /* LR */
    *--sp = 0;                                           /* R12 */
    *--sp = 0;                                           /* R3 */
    *--sp = (uint32_t)((initial_lparam >> 16) & 0xFFFF);/* R2 */
    *--sp = (uint32_t)(initial_sparam);                  /* R1 */
    *--sp = (uint32_t)(initial_msg_type);                /* R0 */

    /* Software-saved registers (M0+ order: R4-R7 above R8-R11 on stack) */
    *--sp = 0; /* R7 */
    *--sp = 0; /* R6 */
    *--sp = 0; /* R5 */
    *--sp = 0; /* R4 */
    *--sp = 0; /* R11 */
    *--sp = 0; /* R10 */
    *--sp = 0; /* R9 */
    *--sp = 0; /* R8 */  /* ← SP */

    return (void *)sp;
}

/* =========================================================================
 * Context switch — Cortex-M0+ assembly
 *
 * M0+ cannot push/pop R8-R11 directly; move them through R4-R7.
 * Push order: R4-R7 first (to higher address), then R8-R11 via R4-R7 (lower).
 * Stack at *p_current_sp_storage: [R8][R9][R10][R11][R4][R5][R6][R7]
 * ========================================================================= */
__attribute__((naked)) void ktos_hal_ContextSwitch(
    void **p_current_sp_storage __attribute__((unused)),
    const void *next_sp          __attribute__((unused)))
{
    __asm volatile (
        /* Save R4-R7 (go to higher stack addresses) */
        "push   {r4, r5, r6, r7}    \n"
        /* Save R8-R11 via R4-R7 (go to lower stack addresses) */
        "mov    r4, r8              \n"
        "mov    r5, r9              \n"
        "mov    r6, r10             \n"
        "mov    r7, r11             \n"
        "push   {r4, r5, r6, r7}   \n"
        /* Store current SP — M0+ forbids str sp,[r0]; move via low register */
        "mov    r4, sp              \n"
        "str    r4, [r0]            \n"
        /* Load next SP */
        "mov    sp, r1              \n"
        /* Restore R8-R11 of next context */
        "pop    {r4, r5, r6, r7}   \n"
        "mov    r8, r4              \n"
        "mov    r9, r5              \n"
        "mov    r10, r6             \n"
        "mov    r11, r7             \n"
        /* Restore R4-R7 of next context */
        "pop    {r4, r5, r6, r7}   \n"
        "bx     lr                 \n"
    );
}

/* =========================================================================
 * Scheduler launch — Cortex-M0+
 * ========================================================================= */
__attribute__((naked)) void ktos_hal_StartScheduler(const void *first_task_sp __attribute__((unused)))
{
    __asm volatile (
        "mov    sp, r0              \n"
        /* Restore R8-R11 */
        "pop    {r4, r5, r6, r7}   \n"
        "mov    r8, r4              \n"
        "mov    r9, r5              \n"
        "mov    r10, r6             \n"
        "mov    r11, r7             \n"
        /* Restore R4-R7 */
        "pop    {r4, r5, r6, r7}   \n"
        /* Pop hardware frame: R0-R3 */
        "pop    {r0, r1, r2, r3}   \n"
        /* Skip R12, LR; jump to PC */
        "pop    {r4}               \n" /* discard R12 */
        "pop    {r4}               \n" /* discard LR  */
        "pop    {r4}               \n" /* PC = task entry */
        "bx     r4                 \n"
    );
}
