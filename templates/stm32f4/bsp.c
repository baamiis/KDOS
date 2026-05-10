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

#include "ktos_hal.h"
#include "stm32f4xx.h"

void ktos_hal_DisableInterrupts(void)
{
    __disable_irq();
}

void ktos_hal_EnableInterrupts(void)
{
    __enable_irq();
}

void *ktos_hal_InitTaskStack(void *p_stack_base,
                          unsigned int stack_size_bytes,
                          void (*task_func_addr)(WORD, WORD, LONG),
                          void (*task_exit_handler_addr)(WORD),
                          WORD initial_msg_type,
                          WORD initial_sparam,
                          LONG initial_lparam)
{
    uint32_t *sp = (uint32_t *)((uint8_t *)p_stack_base + stack_size_bytes);
    sp = (uint32_t *)((uint32_t)sp & ~0x7U); /* 8-byte alignment */

    /* Automatic stacking as performed on exception entry */
    *--sp = 0x01000000U;                 /* xPSR */
    *--sp = (uint32_t)task_func_addr;     /* PC */
    *--sp = (uint32_t)task_exit_handler_addr; /* LR */
    *--sp = 0; /* R12 */
    *--sp = 0; /* R3 */
    *--sp = initial_lparam; /* R2 */
    *--sp = initial_sparam; /* R1 */
    *--sp = initial_msg_type; /* R0 */

    /* Remaining registers R4-R11 saved by context switch */
    for (int i = 0; i < 8; ++i) {
        *--sp = 0;
    }

    return sp;
}

__attribute__((naked)) void ktos_hal_ContextSwitch(void **current_sp_storage, void *next_sp)
{
    __asm volatile (
        "mrs r2, psp\n"           /* Get current process stack pointer */
        "stmdb r2!, {r4-r11}\n"  /* Save callee-saved registers */
        "str r2, [r0]\n"        /* Store current SP */
        "ldr r2, [r1]\n"        /* Load next SP */
        "ldmia r2!, {r4-r11}\n" /* Restore callee-saved regs */
        "msr psp, r2\n"         /* Set PSP to new task */
        "bx lr\n"               /* Return */
    );
}

__attribute__((naked)) void ktos_hal_StartScheduler(void *first_task_sp)
{
    __asm volatile (
        "msr psp, %0\n"          /* Set PSP to task stack */
        "movs r0, #2\n"
        "msr CONTROL, r0\n"      /* Switch to PSP */
        "isb\n"
        "pop {r4-r11}\n"        /* Pop initial r4-r11 */
        "pop {r0-r3, r12, lr, pc}\n" /* Pop remaining regs and start task */
        : : "r" (first_task_sp) : "memory"
    );
    for(;;);
}

void ktos_hal_InitSystemTimer(void (*isr)(void))
{
    SysTick_Config(SystemCoreClock / 1000);
    (void)isr; /* vector table should point to isr */
}
