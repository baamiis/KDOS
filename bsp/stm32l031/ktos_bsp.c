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
 * @brief KTOS Board Support Package — STM32L031 (ARM Cortex-M0+)
 *
 * Supported boards: Nucleo-L031K6.
 *
 * | Property  | Value                              |
 * |-----------|------------------------------------|
 * | Core      | ARM Cortex-M0+                     |
 * | RAM       | 8 KB                               |
 * | Flash     | 32 KB                              |
 * | Clock     | 4 MHz (MSI default)                |
 * | Toolchain | arm-none-eabi-gcc                  |
 *
 * ### Build
 * @code
 * arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb --specs=nosys.specs ...
 * @endcode
 *
 * ### Cortex-M0+ assembly constraints
 * - Only low registers (R0-R7) can be used with most Thumb instructions.
 * - R8-R11 must be moved to low registers via @c MOV before push/store.
 * - @c STR with SP as source is not permitted — use @c MOV R2, SP first.
 *
 * ### SysTick ISR wiring
 * @code
 * void SysTick_Handler(void) { ktos_timer_irq_handler(); }
 * @endcode
 *
 * @defgroup ktos_bsp_stm32l031 KTOS BSP — STM32L031 (Cortex-M0+)
 * @ingroup  ktos_hal
 */

#include "../../core/ktos_hal.h"
#include <stdint.h>

/* SysTick registers */
#define SYSTICK_BASE    0xE000E010UL
#define SYSTICK_CTRL    (*(volatile uint32_t *)(SYSTICK_BASE + 0x00))
#define SYSTICK_LOAD    (*(volatile uint32_t *)(SYSTICK_BASE + 0x04))
#define SYSTICK_VAL     (*(volatile uint32_t *)(SYSTICK_BASE + 0x08))

/** @brief Disable all maskable interrupts (CPSID I). */
void ktos_hal_DisableInterrupts(void)
{
    __asm volatile ("cpsid i" ::: "memory");
}

/** @brief Re-enable maskable interrupts (CPSIE I). */
void ktos_hal_EnableInterrupts(void)
{
    __asm volatile ("cpsie i" ::: "memory");
}

/**
 * @brief Configure SysTick for a 1 ms interrupt at 4 MHz MSI.
 *
 * 4MHz MSI / 1000 = 4000 ticks per 1ms.
 */
void ktos_hal_InitSystemTimer(void (*timer_isr_addr)(void))
{
    SYSTICK_LOAD = 4000 - 1;
    SYSTICK_VAL  = 0;
    SYSTICK_CTRL = (1 << 2) | (1 << 1) | (1 << 0);
    (void)timer_isr_addr;
    /* void SysTick_Handler(void) { ktos_timer_irq_handler(); } */
}

/**
 * @brief Build the initial Cortex-M0+ stack frame for a new task.
 *
 * Identical layout to the STM32F030 (Cortex-M0) BSP.  Both cores share the
 * same exception frame format and Thumb instruction set (M0+ is a superset
 * of M0 with no additional saved registers).
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
    sp = (uint32_t *)((uint32_t)sp & ~0x7UL);

    *--sp = 0x01000000UL;                              /* xPSR: Thumb bit */
    *--sp = (uint32_t)task_func_addr;                  /* PC */
    *--sp = (uint32_t)task_exit_handler_addr;          /* LR */
    *--sp = 0;                                         /* R12 */
    *--sp = 0;                                         /* R3 */
    *--sp = (uint32_t)((initial_lparam >> 16) & 0xFFFF); /* R2 = lParam high */
    *--sp = (uint32_t)(initial_sparam);                /* R1 */
    *--sp = (uint32_t)(initial_msg_type);              /* R0 */

    /* R4-R11 */
    for (int i = 0; i < 8; i++) *--sp = 0;

    return (void *)sp;
}

/**
 * @brief Cortex-M0+ cooperative context switch.
 *
 * Identical to the STM32F030 (Cortex-M0) implementation.
 * Cortex-M0+ has the same Thumb-2 instruction restrictions as Cortex-M0.
 */
__attribute__((naked)) void ktos_hal_ContextSwitch(
    void **p_current_sp_storage __attribute__((unused)),
    const void *next_sp          __attribute__((unused)))
{
    __asm volatile (
        "push   {r4-r7}             \n"
        "mov    r4, r8              \n"
        "mov    r5, r9              \n"
        "mov    r6, r10             \n"
        "mov    r7, r11             \n"
        "push   {r4-r7}             \n"
        "mov    r2, sp              \n"
        "str    r2, [r0]            \n" /* *p_current_sp_storage = SP */
        "mov    sp, r1              \n" /* SP = next_sp               */
        "pop    {r4-r7}             \n"
        "mov    r8,  r4             \n"
        "mov    r9,  r5             \n"
        "mov    r10, r6             \n"
        "mov    r11, r7             \n"
        "pop    {r4-r7}             \n"
        "bx     lr                  \n"
    );
}

/**
 * @brief Load the first task's stack and begin execution.  Never returns.
 *
 * Identical to the STM32F030 implementation.
 */
__attribute__((naked)) void ktos_hal_StartScheduler(const void *first_task_sp __attribute__((unused)))
{
    __asm volatile (
        "mov    sp, r0              \n"
        "pop    {r4-r7}             \n"
        "mov    r8,  r4             \n"
        "mov    r9,  r5             \n"
        "mov    r10, r6             \n"
        "mov    r11, r7             \n"
        "pop    {r4-r7}             \n"
        "pop    {r0-r3}             \n"
        "pop    {r4}                \n" /* R12 */
        "pop    {r5}                \n" /* LR  */
        "pop    {r6}                \n" /* PC  */
        "pop    {r7}                \n" /* xPSR - discard */
        "bx     r6                  \n"
    );
}
