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
 * @brief KTOS Board Support Package — MSP430G2553 (MSP430 16-bit)
 *
 * Supported boards: MSP430 LaunchPad (MSP-EXP430G2).
 *
 * | Property  | Value                              |
 * |-----------|------------------------------------|
 * | Core      | MSP430 16-bit RISC                 |
 * | RAM       | 512 B                              |
 * | Flash     | 16 KB                              |
 * | Clock     | 1 MHz (DCO default)                |
 * | Toolchain | msp430-elf-gcc                     |
 *
 * ### Build
 * @code
 * msp430-elf-gcc -mmcu=msp430g2553 ...
 * @endcode
 *
 * ### Architecture notes
 * - R0=PC, R1=SP, R2=SR, R3=CG (constant generator)
 * - Callee-saved: R4-R11.  Caller-saved / args: R12-R15.
 * - Calling convention: MsgType(WORD)→R12, Param1(WORD)→R13, Param2(LONG)→R14:R15.
 * - Context switch saves R4-R11 + SR (R2).
 * - A trampoline (@c ktos_task_launch in ktos_bsp_asm.S) is needed for the
 *   first dispatch because R12-R15 are not preserved across context switches.
 *   Arguments are stashed in R4-R8 (callee-saved) until the trampoline
 *   moves them to the conventional argument registers before calling the task.
 *
 * ### Timer ISR wiring
 * @code
 * #pragma vector = TIMER0_A0_VECTOR
 * __interrupt void Timer_A_CCR0_ISR(void) { ktos_timer_irq_handler(); }
 * @endcode
 *
 * @defgroup ktos_bsp_msp430 KTOS BSP — MSP430G2553
 * @ingroup  ktos_hal
 */

#include "../../core/ktos_hal.h"
#include <msp430.h>
#include <stdint.h>

/* Forward declaration — implemented in ktos_bsp_asm.S */
extern void ktos_task_launch(void);

/** @brief Disable all maskable interrupts (MSP430 DINT). */
void ktos_hal_DisableInterrupts(void)
{
    __asm__ volatile ("dint" ::: "memory");
}

/** @brief Re-enable maskable interrupts (MSP430 EINT). */
void ktos_hal_EnableInterrupts(void)
{
    __asm__ volatile ("eint" ::: "memory");
}

/**
 * @brief Configure Timer_A CCR0 for a 1 ms interrupt at 1 MHz SMCLK.
 *
 * SMCLK defaults to 1 MHz on reset.  CCR0 = 1000-1 gives exactly 1 ms.
 * ISR wiring:
 * @code
 * #pragma vector=TIMER0_A0_VECTOR
 * __interrupt void Timer_A_CCR0_ISR(void) { ktos_timer_irq_handler(); }
 * @endcode
 */
void ktos_hal_InitSystemTimer(void (*timer_isr_addr)(void))
{
    TACTL  = TASSEL_2 | MC_1 | TACLR;  /* SMCLK, up mode, clear */
    TACCR0 = 1000 - 1;                  /* 1ms at 1MHz SMCLK */
    TACCTL0 = CCIE;                     /* CCR0 interrupt enable */
    (void)timer_isr_addr;
}

/**
 * @brief Build the initial MSP430 stack frame for a new task.
 *
 * Stack layout (SP points to bottom entry after setup, first POP gives SR):
 * ```
 * high addr → [task_exit_handler_addr]   on stack when task returns
 *             [ktos_task_launch]          popped by RET in StartScheduler
 *             [R11 = 0]
 *             [R10 = 0]
 *             [R9  = 0]
 *             [R8  = task_func_addr]
 *             [R7  = Param2 >> 16]
 *             [R6  = Param2 & 0xFFFF]
 *             [R5  = initial_sparam]
 *             [R4  = initial_msg_type]
 * low addr  → [SR  = 0x0008 (GIE)]       ← SP after setup, popped first
 * ```
 *
 * The trampoline (@c ktos_task_launch) moves R4-R8 → R12-R15 and jumps
 * to the actual task function via R8.
 */
void *ktos_hal_InitTaskStack(void *p_stack_base,
                              unsigned int stack_size_bytes,
                              WORD (*task_func_addr)(WORD, WORD, LONG),
                              void (*task_exit_handler_addr)(WORD),
                              WORD initial_msg_type,
                              WORD initial_sparam,
                              LONG initial_lparam)
{
    /* word-aligned */
    uint16_t *sp = (uint16_t *)((uint8_t *)p_stack_base + stack_size_bytes);
    sp = (uint16_t *)((uintptr_t)sp & ~(uintptr_t)1);

    *--sp = (uint16_t)(uintptr_t)task_exit_handler_addr;
    *--sp = (uint16_t)(uintptr_t)ktos_task_launch;
    *--sp = 0;                                           /* R11 */
    *--sp = 0;                                           /* R10 */
    *--sp = 0;                                           /* R9  */
    *--sp = (uint16_t)(uintptr_t)task_func_addr;         /* R8  */
    *--sp = (uint16_t)((uint32_t)initial_lparam >> 16);  /* R7 = Param2 high */
    *--sp = (uint16_t)((uint32_t)initial_lparam & 0xFFFF); /* R6 = Param2 low */
    *--sp = (uint16_t)initial_sparam;                    /* R5  */
    *--sp = (uint16_t)initial_msg_type;                  /* R4  */
    *--sp = 0x0008U;                                     /* SR with GIE */

    return (void *)sp;
}

/**
 * @brief MSP430 cooperative context switch.
 *
 * p_current_sp_storage arrives in R12, next_sp in R13 (MSP430 calling conv).
 * Saves R4-R11 and SR (R2), stores current SP, loads next SP, restores.
 *
 * In MSP430 GAS:
 *   @c push @c r2 pushes SR.
 *   @c mov @c r1, @c 0(r12) stores SP (R1) to address in R12.
 *   @c mov @c r13, @c r1 sets SP = next_sp.
 */
__attribute__((naked)) void ktos_hal_ContextSwitch(
    void **p_current_sp_storage __attribute__((unused)),
    const void *next_sp          __attribute__((unused)))
{
    __asm__ volatile (
        "push   r4              \n"
        "push   r5              \n"
        "push   r6              \n"
        "push   r7              \n"
        "push   r8              \n"
        "push   r9              \n"
        "push   r10             \n"
        "push   r11             \n"
        "push   r2              \n"  /* SR */
        "mov    r1, 0(r12)      \n"  /* *p_current_sp_storage = SP */
        "mov    r13, r1         \n"  /* SP = next_sp */
        "pop    r2              \n"  /* restore SR */
        "pop    r11             \n"
        "pop    r10             \n"
        "pop    r9              \n"
        "pop    r8              \n"
        "pop    r7              \n"
        "pop    r6              \n"
        "pop    r5              \n"
        "pop    r4              \n"
        "ret                    \n"
    );
}

/**
 * @brief Load the first task's stack and begin execution.  Never returns.
 *
 * first_task_sp arrives in R12.
 * Sets SP = R12, then pops SR + R4-R11, then RET into ktos_task_launch.
 */
__attribute__((naked)) void ktos_hal_StartScheduler(
    const void *first_task_sp __attribute__((unused)))
{
    __asm__ volatile (
        "mov    r12, r1         \n"  /* SP = first_task_sp */
        "pop    r2              \n"  /* restore SR */
        "pop    r11             \n"
        "pop    r10             \n"
        "pop    r9              \n"
        "pop    r8              \n"
        "pop    r7              \n"
        "pop    r6              \n"
        "pop    r5              \n"
        "pop    r4              \n"
        "ret                    \n"  /* jump to ktos_task_launch */
    );
}
