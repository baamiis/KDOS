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
 * @brief KTOS Board Support Package — ATtiny85 (AVR 8-bit)
 *
 * Supported boards: Digispark.
 *
 * | Property  | Value                              |
 * |-----------|------------------------------------|
 * | Core      | AVR 8-bit                          |
 * | RAM       | 512 B                              |
 * | Flash     | 8 KB                               |
 * | Clock     | 8 MHz (internal)                   |
 * | Toolchain | avr-gcc                            |
 *
 * ### Build
 * @code
 * avr-gcc -mmcu=attiny85 -DF_CPU=8000000UL ...
 * @endcode
 *
 * ### Timer ISR wiring
 * ATtiny85 uses Timer0 for the system tick (Timer1 is not available for CTC
 * with the same flexibility).  Applications must define:
 * @code
 * ISR(TIMER0_COMPA_vect) { ktos_timer_irq_handler(); }
 * @endcode
 *
 * @note ATtiny85 has no UART — applications use GPIO (e.g. SoftwareSerial or
 *       USI-based UART).  The canonical demo blinks an LED on PB0.
 *
 * @note ATtiny85 has 8KB flash (4K words) — 2-byte PC, same as ATmega328P.
 *
 * @defgroup ktos_bsp_attiny85 KTOS BSP — AVR (ATtiny85)
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
 * @brief Configure Timer0 in CTC mode for a 1 ms interrupt at 8 MHz.
 *
 * Prescaler 64, OCR0A = 124 → 1 ms at 8 MHz.
 * ATtiny85 uses @c TIMSK (not @c TIMSK0) for the interrupt enable register.
 */
void ktos_hal_InitSystemTimer(void (*timer_isr_addr)(void))
{
    TCCR0A = (1 << WGM01);                      /* CTC mode */
    TCCR0B = (1 << CS01) | (1 << CS00);         /* prescaler 64 */
    OCR0A  = 124;                                /* 1ms at 8MHz */
    TIMSK |= (1 << OCIE0A);                      /* ATtiny85 uses TIMSK not TIMSK0 */
    (void)timer_isr_addr;
}

/**
 * @brief Build the initial AVR stack frame for a new task.
 *
 * ATtiny85 has 8KB flash (4K words) — 2-byte PC, identical layout to
 * the ATmega328P BSP.
 *
 * Stack layout (top = lowest address):
 * ```
 * [ R31..R18 ]   R24:R25=MsgType, R22:R23=sParam, R18:R21=lParam
 * [ R17..R1, R0 ]
 * [ SREG ]       I-bit set
 * [ exit PCH ]
 * [ exit PCL ]
 * [ task PCH ]
 * [ task PCL ]
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

    uint16_t pc     = (uint16_t)task_func_addr;
    uint16_t exit_pc = (uint16_t)task_exit_handler_addr;

    *sp-- = (uint8_t)(pc & 0xFF);
    *sp-- = (uint8_t)((pc >> 8) & 0xFF);
    *sp-- = (uint8_t)(exit_pc & 0xFF);
    *sp-- = (uint8_t)((exit_pc >> 8) & 0xFF);

    *sp-- = 0x80; /* SREG with I-bit */

    for (int i = 0; i < 24; i++) *sp-- = 0x00;
    *sp-- = (uint8_t)(initial_msg_type & 0xFF);
    *sp-- = (uint8_t)((initial_msg_type >> 8) & 0xFF);
    *sp-- = (uint8_t)(initial_sparam & 0xFF);
    *sp-- = (uint8_t)((initial_sparam >> 8) & 0xFF);
    *sp-- = (uint8_t)(initial_lparam & 0xFF);
    *sp-- = (uint8_t)((initial_lparam >> 8) & 0xFF);
    *sp-- = (uint8_t)((initial_lparam >> 16) & 0xFF);
    *sp-- = (uint8_t)((initial_lparam >> 24) & 0xFF);

    return (void *)sp;
}

/**
 * @brief Save/restore all 32 AVR registers + SREG and swap stack pointers.
 *
 * Identical to ATmega328P.
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
        "movw   r30, r22            \n"
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
