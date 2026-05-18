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
 * @brief KTOS Board Support Package — ATmega2560 (AVR 8-bit)
 *
 * Supported boards: Arduino Mega 2560.
 *
 * | Property  | Value                              |
 * |-----------|------------------------------------|
 * | Core      | AVR 8-bit                          |
 * | RAM       | 8 KB                               |
 * | Flash     | 256 KB                             |
 * | Clock     | 16 MHz                             |
 * | Toolchain | avr-gcc                            |
 *
 * ### Build
 * @code
 * avr-gcc -mmcu=atmega2560 -DF_CPU=16000000UL ...
 * @endcode
 *
 * ### Timer ISR wiring
 * @code
 * ISR(TIMER1_COMPA_vect) { ktos_timer_irq_handler(); }
 * @endcode
 *
 * @note ATmega2560 has 256KB flash (128K words), so the hardware pushes a
 *       3-byte PC on CALL/RET.  InitTaskStack must push 3 bytes per return
 *       address (PCL, PCH, PCEXT).
 *
 * @defgroup ktos_bsp_avr2560 KTOS BSP — AVR (ATmega2560)
 * @ingroup  ktos_hal
 */

#include "../../core/ktos_hal.h"
#include <avr/io.h>
#include <avr/interrupt.h>

/** @brief Disable all interrupts (AVR @c cli). */
void ktos_hal_DisableInterrupts(void) { cli(); }

/** @brief Re-enable interrupts (AVR @c sei). */
void ktos_hal_EnableInterrupts(void) { sei(); }

/**
 * @brief Configure Timer1 in CTC mode for a 1 ms interrupt.
 *
 * Prescaler 64, OCR1A = 249 → 1 ms at 16 MHz.
 * ISR vector @c TIMER1_COMPA_vect must be defined in the application.
 */
void ktos_hal_InitSystemTimer(void (*timer_isr_addr)(void))
{
    TCCR1A = 0;
    TCCR1B = (1 << WGM12) | (1 << CS11) | (1 << CS10); /* CTC, prescaler 64 */
    OCR1A  = 249;                                         /* 1ms at 16MHz */
    TIMSK1 = (1 << OCIE1A);
    (void)timer_isr_addr;
}

/**
 * @brief Build the initial AVR stack frame for a new task.
 *
 * ATmega2560 has 256KB flash, so the hardware uses a 3-byte PC on CALL/RET.
 * Each return address consumes 3 bytes on the stack (PCL, PCH, PCEXT).
 *
 * Stack layout (top = lowest address, popped first by StartScheduler):
 * ```
 * [ R31..R18 ]         14 registers: args in R24:R25, R22:R23, R18:R21
 * [ R17..R1, R0 ]      18 registers zeroed
 * [ SREG ]             status register (I-bit set)
 * [ exit PCL ]         exit handler 3-byte PC
 * [ exit PCH ]
 * [ exit PCEXT ]
 * [ task PCL ]         task function 3-byte PC
 * [ task PCH ]
 * [ task PCEXT ]
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
    uint8_t *sp = (uint8_t *)p_stack_base + stack_size_bytes - 1;

    uint32_t tpc = (uint32_t)(uintptr_t)task_func_addr;
    uint32_t epc = (uint32_t)(uintptr_t)task_exit_handler_addr;

    /* 3-byte PC: LSB first (ATmega2560 pushes PCL, PCH, PCEXT) */
    *sp-- = (uint8_t)(tpc & 0xFF);
    *sp-- = (uint8_t)((tpc >> 8) & 0xFF);
    *sp-- = (uint8_t)((tpc >> 16) & 0xFF);
    *sp-- = (uint8_t)(epc & 0xFF);
    *sp-- = (uint8_t)((epc >> 8) & 0xFF);
    *sp-- = (uint8_t)((epc >> 16) & 0xFF);

    /* SREG with I-bit set (interrupts enabled on task entry) */
    *sp-- = 0x80;

    /* R0-R17 zeroed */
    for (int i = 0; i < 18; i++) *sp-- = 0x00;

    /* R24:R25 = MsgType (first arg) */
    *sp-- = (uint8_t)(initial_msg_type & 0xFF);
    *sp-- = (uint8_t)((initial_msg_type >> 8) & 0xFF);

    /* R22:R23 = sParam (second arg) */
    *sp-- = (uint8_t)(initial_sparam & 0xFF);
    *sp-- = (uint8_t)((initial_sparam >> 8) & 0xFF);

    /* R18:R21 = lParam (third arg, 32-bit) */
    *sp-- = (uint8_t)(initial_lparam & 0xFF);
    *sp-- = (uint8_t)((initial_lparam >> 8) & 0xFF);
    *sp-- = (uint8_t)((initial_lparam >> 16) & 0xFF);
    *sp-- = (uint8_t)((initial_lparam >> 24) & 0xFF);

    /* R26-R31 zeroed */
    for (int i = 0; i < 6; i++) *sp-- = 0x00;

    return (void *)sp;
}

/**
 * @brief Save/restore all 32 AVR registers + SREG and swap stack pointers.
 *
 * Identical to ATmega328P — the context switch assembly does not depend
 * on PC width; only the stack frame setup differs for 3-byte PC.
 */
void ktos_hal_ContextSwitch(void **p_current_sp_storage, const void *next_sp)
{
    __asm__ volatile (
        "in     r0, __SREG__        \n"
        "cli                        \n"
        "push   r0                  \n"
        "push   r1                  \n"
        "push   r2                  \n"
        "push   r3                  \n"
        "push   r4                  \n"
        "push   r5                  \n"
        "push   r6                  \n"
        "push   r7                  \n"
        "push   r8                  \n"
        "push   r9                  \n"
        "push   r10                 \n"
        "push   r11                 \n"
        "push   r12                 \n"
        "push   r13                 \n"
        "push   r14                 \n"
        "push   r15                 \n"
        "push   r16                 \n"
        "push   r17                 \n"
        "push   r18                 \n"
        "push   r19                 \n"
        "push   r20                 \n"
        "push   r21                 \n"
        "push   r22                 \n"
        "push   r23                 \n"
        "push   r24                 \n"
        "push   r25                 \n"
        "push   r26                 \n"
        "push   r27                 \n"
        "push   r28                 \n"
        "push   r29                 \n"
        "push   r30                 \n"
        "push   r31                 \n"
        "in     r26, __SP_L__       \n"
        "in     r27, __SP_H__       \n"
        "st     X+, r26             \n"
        "st     X,  r27             \n"
        "movw   r30, r22            \n"  /* next_sp in r22:r23 */
        "out    __SP_H__, r31       \n"
        "out    __SP_L__, r30       \n"
        "pop    r31                 \n"
        "pop    r30                 \n"
        "pop    r29                 \n"
        "pop    r28                 \n"
        "pop    r27                 \n"
        "pop    r26                 \n"
        "pop    r25                 \n"
        "pop    r24                 \n"
        "pop    r23                 \n"
        "pop    r22                 \n"
        "pop    r21                 \n"
        "pop    r20                 \n"
        "pop    r19                 \n"
        "pop    r18                 \n"
        "pop    r17                 \n"
        "pop    r16                 \n"
        "pop    r15                 \n"
        "pop    r14                 \n"
        "pop    r13                 \n"
        "pop    r12                 \n"
        "pop    r11                 \n"
        "pop    r10                 \n"
        "pop    r9                  \n"
        "pop    r8                  \n"
        "pop    r7                  \n"
        "pop    r6                  \n"
        "pop    r5                  \n"
        "pop    r4                  \n"
        "pop    r3                  \n"
        "pop    r2                  \n"
        "pop    r1                  \n"
        "pop    r0                  \n"
        "out    __SREG__, r0        \n"
        "pop    r0                  \n"
        "ret                        \n"
        :
        : "x" (p_current_sp_storage), "z" (next_sp)
        : "memory"
    );
}

/**
 * @brief Load the first task's stack and begin execution.  Never returns.
 */
void ktos_hal_StartScheduler(const void *first_task_sp)
{
    __asm__ volatile (
        "out    __SP_H__, %B0       \n"
        "out    __SP_L__, %A0       \n"
        :
        : "r" (first_task_sp)
    );

    __asm__ volatile (
        "pop    r31                 \n"
        "pop    r30                 \n"
        "pop    r29                 \n"
        "pop    r28                 \n"
        "pop    r27                 \n"
        "pop    r26                 \n"
        "pop    r25                 \n"
        "pop    r24                 \n"
        "pop    r23                 \n"
        "pop    r22                 \n"
        "pop    r21                 \n"
        "pop    r20                 \n"
        "pop    r19                 \n"
        "pop    r18                 \n"
        "pop    r17                 \n"
        "pop    r16                 \n"
        "pop    r15                 \n"
        "pop    r14                 \n"
        "pop    r13                 \n"
        "pop    r12                 \n"
        "pop    r11                 \n"
        "pop    r10                 \n"
        "pop    r9                  \n"
        "pop    r8                  \n"
        "pop    r7                  \n"
        "pop    r6                  \n"
        "pop    r5                  \n"
        "pop    r4                  \n"
        "pop    r3                  \n"
        "pop    r2                  \n"
        "pop    r1                  \n"
        "pop    r0                  \n"
        "out    __SREG__, r0        \n"
        "pop    r0                  \n"
        "ret                        \n"
    );

    while (1);
}
