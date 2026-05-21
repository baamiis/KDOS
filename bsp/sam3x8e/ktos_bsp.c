/*
 * KTOS — Tiny Cooperative Task Switcher
 * Copyright (C) 2004-2025 Khalid Hamdou / BAAMIIS LIMITED
 * All rights reserved.
 *
 * Author:  Khalid Hamdou
 * Company: BAAMIIS LIMITED
 * GitHub:  https://github.com/baamiis/KTOS
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of KTOS.
 *
 * KTOS is dual-licensed:
 *
 *   Open Source: GNU General Public License v3 (see LICENSE)
 *   Commercial:  Proprietary license available (see COMMERCIAL_LICENSE)
 *
 * For open source use, this program is free software: you can
 * redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * For commercial/proprietary use without GPL obligations, a Commercial
 * License must be obtained from BAAMIIS LIMITED.
 * Contact: baamiis7@gmail.com
 *
 * KTOS is the original work of Khalid Hamdou. No person or organisation
 * may claim authorship or ownership of this software.
 */

/**
 * @file ktos_bsp.c
 * @brief KTOS Board Support Package — Atmel SAM3X8E (ARM Cortex-M3)
 *
 * Supported boards: Arduino Due.
 *
 * | Property  | Value                         |
 * |-----------|-------------------------------|
 * | Core      | ARM Cortex-M3                 |
 * | RAM       | 96 KB (64 KB + 32 KB banks)   |
 * | Flash     | 512 KB (2 × 256 KB banks)     |
 * | Clock     | 84 MHz (Arduino Due default)  |
 * | Toolchain | arm-none-eabi-gcc             |
 *
 * The Cortex-M3 architecture is identical to the STM32F103 — the
 * context-switch + task-frame logic in this file is byte-for-byte the
 * same as `bsp/stm32f103/ktos_bsp.c`.  Unlike the STM32 BSPs which use
 * SysTick, the SAM3X8E BSP drives the 1 ms tick from **TC0 channel 0**
 * so it can coexist with Arduino-SAM's runtime (which owns SysTick to
 * drive `millis()`).
 *
 * ### Build
 * @code
 * arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb --specs=nosys.specs ...
 * @endcode
 *
 * ### TC0 ISR wiring
 * Add this to your application (not inside the BSP):
 * @code
 * void TC0_Handler(void) {
 *     (void)TC0->TC_CHANNEL[0].TC_SR;   // clear the compare flag
 *     ktos_timer_irq_handler();
 * }
 * @endcode
 *
 * @defgroup ktos_bsp_sam3x KTOS BSP — SAM3X8E (Arduino Due)
 * @ingroup  ktos_hal
 */

#include "../../core/ktos_hal.h"
#include <stdint.h>

/* =========================================================================
 * Interrupt control
 * ========================================================================= */

/** @brief Disable all maskable interrupts (Cortex-M @c CPSID I). */
void ktos_hal_DisableInterrupts(void)
{
    __asm volatile ("cpsid i" ::: "memory");
}

/** @brief Re-enable maskable interrupts (Cortex-M @c CPSIE I). */
void ktos_hal_EnableInterrupts(void)
{
    __asm volatile ("cpsie i" ::: "memory");
}

/* =========================================================================
 * System timer — TC0 channel 0, 1 ms tick at 84 MHz
 *
 * Why TC0 and not SysTick?  On the SAM3X8E the Cortex-M3 SysTick is
 * frequently claimed by upper-layer code (e.g. the Arduino-SAM core's
 * `millis()`).  Using a dedicated Timer/Counter peripheral lets KTOS
 * coexist with any startup environment without fighting over a shared
 * interrupt vector.
 *
 * Clock chain: MCK = 84 MHz → TC TIMER_CLOCK2 (= MCK/8) = 10.5 MHz.
 * RC = 10 500 → compare match every 1.0 ms exactly.
 *
 * The application MUST define TC0_Handler() to call
 * @c ktos_timer_irq_handler() and MUST read TC0->TC_CHANNEL[0].TC_SR
 * to clear the compare flag (otherwise the interrupt re-fires
 * immediately).  See examples for the canonical wiring.
 * ========================================================================= */

/* Peripheral Management Controller (PMC) — for enabling TC0 clock. */
#define SAM_PMC_PCER0   (*(volatile uint32_t *)0x400E0610UL)

/* TC0 channel 0 register block (datasheet §36). */
#define TC0_CH0_BASE    0x40080000UL
#define TC0_CH0_CCR     (*(volatile uint32_t *)(TC0_CH0_BASE + 0x00))
#define TC0_CH0_CMR     (*(volatile uint32_t *)(TC0_CH0_BASE + 0x04))
#define TC0_CH0_RC      (*(volatile uint32_t *)(TC0_CH0_BASE + 0x1C))
#define TC0_CH0_SR      (*(volatile uint32_t *)(TC0_CH0_BASE + 0x20))
#define TC0_CH0_IER     (*(volatile uint32_t *)(TC0_CH0_BASE + 0x24))
#define TC0_CH0_IDR     (*(volatile uint32_t *)(TC0_CH0_BASE + 0x28))

/* TC_CCR (Channel Control). */
#define TC_CCR_CLKEN    (1u << 0)
#define TC_CCR_CLKDIS   (1u << 1)
#define TC_CCR_SWTRG    (1u << 2)

/* TC_CMR (Channel Mode) — waveform mode, MCK/8, reset on RC. */
#define TC_CMR_TCCLKS_TIMER_CLOCK2  (1u << 0)   /* MCK/8 */
#define TC_CMR_WAVSEL_UP_RC         (2u << 13)
#define TC_CMR_WAVE                 (1u << 15)

/* TC interrupt: RC compare. */
#define TC_IER_CPCS     (1u << 4)

/* Cortex-M3 NVIC — enable IRQ 27 (TC0 channel 0). */
#define NVIC_ISER0      (*(volatile uint32_t *)0xE000E100UL)
#define TC0_IRQ_NUMBER  27u

/**
 * @brief Configure TC0 channel 0 for a 1 ms compare interrupt at 84 MHz.
 *
 * @code
 * void TC0_Handler(void) {
 *     (void)TC0->TC_CHANNEL[0].TC_SR;     // clear flag
 *     ktos_timer_irq_handler();
 * }
 * @endcode
 */
void ktos_hal_InitSystemTimer(void (*timer_isr_addr)(void))
{
    /* Enable PMC clock to TC0 channel 0 (peripheral ID 27). */
    SAM_PMC_PCER0 = (1u << TC0_IRQ_NUMBER);

    /* Disable channel before reconfiguring. */
    TC0_CH0_CCR = TC_CCR_CLKDIS;

    /* Waveform mode, MCK/8, reset on RC match. */
    TC0_CH0_CMR = TC_CMR_TCCLKS_TIMER_CLOCK2 |
                  TC_CMR_WAVE                 |
                  TC_CMR_WAVSEL_UP_RC;

    /* 84 MHz / 8 = 10.5 MHz → 10 500 ticks per 1 ms. */
    TC0_CH0_RC = 10500u;

    /* Enable RC compare interrupt; mask everything else. */
    TC0_CH0_IDR = 0xFFFFFFFFu;
    TC0_CH0_IER = TC_IER_CPCS;

    /* Enable TC0 channel 0 IRQ in the NVIC. */
    NVIC_ISER0 = (1u << TC0_IRQ_NUMBER);

    /* Enable the clock and software-trigger the counter. */
    TC0_CH0_CCR = TC_CCR_CLKEN | TC_CCR_SWTRG;

    (void)timer_isr_addr;
}

/* =========================================================================
 * Task stack initialisation
 * ========================================================================= */

/**
 * @brief Build the initial Cortex-M3 stack frame for a new task.
 *
 * Cortex-M hardware automatically pushes/pops R0-R3, R12, LR, PC, xPSR on
 * exceptions.  KTOS manually saves/restores R4-R11.  The synthesised frame:
 * ```
 * (low address / top of stack)
 *   R4-R11   (zeroed)      ← software-saved, restored by ContextSwitch
 *   R0       (MsgType)     ← first task argument
 *   R1       (Param1)
 *   R2       (Param2 low)
 *   R3       (Param2 high)
 *   R12      (0)
 *   LR       (exit handler)
 *   PC       (task entry)
 *   xPSR     (0x01000000) ← Thumb bit set
 * (high address / bottom of stack)
 * ```
 */
void *ktos_hal_InitTaskStack(void *p_stack_base,
                              unsigned int stack_size_bytes,
                              WORD (*task_func_addr)(WORD, WORD, LONG),
                              void (*task_exit_handler_addr)(WORD),
                              WORD initial_msg_type,
                              WORD initial_sparam,
                              LONG initial_lparam)
{
    uint32_t *sp = (uint32_t *)((uint8_t *)p_stack_base + stack_size_bytes);
    sp = (uint32_t *)((uint32_t)sp & ~0x7UL); /* 8-byte alignment */

    /* Hardware exception frame */
    *--sp = 0x01000000UL;                                /* xPSR — Thumb bit set */
    *--sp = (uint32_t)task_func_addr;                    /* PC */
    *--sp = (uint32_t)task_exit_handler_addr;            /* LR */
    *--sp = 0;                                           /* R12 */
    *--sp = 0;                                           /* R3 */
    *--sp = (uint32_t)((initial_lparam >> 16) & 0xFFFF); /* R2 */
    *--sp = (uint32_t)(initial_sparam);                  /* R1 */
    *--sp = (uint32_t)(initial_msg_type);                /* R0 */

    /* Software-saved registers R4-R11 */
    *--sp = 0; /* R11 */
    *--sp = 0; /* R10 */
    *--sp = 0; /* R9  */
    *--sp = 0; /* R8  */
    *--sp = 0; /* R7  */
    *--sp = 0; /* R6  */
    *--sp = 0; /* R5  */
    *--sp = 0; /* R4  */

    return (void *)sp;
}

/* =========================================================================
 * Context switch — Cortex-M3 assembly
 * ========================================================================= */

/**
 * @brief Cortex-M3 cooperative context switch.
 *
 * Pushes R4-R11 onto the current stack, stores SP into
 * @c *p_current_sp_storage, loads @p next_sp into SP, pops R4-R11 of the
 * new context, and returns (BX LR) into it.
 *
 * @note Naked function — no compiler-generated prologue/epilogue.
 */
__attribute__((naked)) void ktos_hal_ContextSwitch(
    void **p_current_sp_storage __attribute__((unused)),
    const void *next_sp          __attribute__((unused)))
{
    __asm volatile (
        "push   {r4-r11}            \n" /* Save callee-saved registers  */
        "str    sp, [r0]            \n" /* *p_current_sp_storage = SP   */
        "mov    sp, r1              \n" /* SP = next_sp                 */
        "pop    {r4-r11}            \n" /* Restore new context          */
        "bx     lr                  \n" /* Return into new task         */
    );
}

/* =========================================================================
 * Scheduler launch
 * ========================================================================= */

/**
 * @brief Load the first task's stack and begin execution.  Never returns.
 *
 * Sets SP to @p first_task_sp, restores R4-R11, then pops the hardware
 * exception frame (R0-R3, R12, LR, PC, xPSR) to start the task.
 *
 * @note Naked function — no compiler-generated prologue/epilogue.
 */
__attribute__((naked)) void ktos_hal_StartScheduler(const void *first_task_sp __attribute__((unused)))
{
    __asm volatile (
        "mov    sp, r0              \n" /* Set SP to first task stack   */
        "pop    {r4-r11}            \n" /* Restore R4-R11               */
        "pop    {r0-r3, r12, lr}    \n" /* Restore R0-R3, R12, LR       */
        "pop    {pc}                \n" /* Jump to task (pop PC + xPSR) */
    );
}
