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
 * @file main.c
 * @brief KTOS LED blink demo for ATtiny85 (AVR 8-bit). No UART — LED on PB0.
 *
 * Toggles PB0 every 500 ms (1 Hz blink) using the KTOS 1 ms timer tick.
 *
 * Expected output: PB0 toggles every 500ms (1Hz blink).
 */

#include "../../../core/ktos.h"
#include "../../../core/ktos_hal.h"
#include <avr/io.h>
#include <avr/interrupt.h>

extern void ktos_timer_irq_handler(void);

ISR(TIMER0_COMPA_vect) { ktos_timer_irq_handler(); }

__attribute__((noreturn)) void ktos_Emergency(const char *msg)
{
    (void)msg;
    /* Fast blink to signal error */
    DDRB |= (1 << PB0);
    for (;;) {
        PORTB ^= (1 << PB0);
        for (volatile uint32_t i = 0; i < 50000UL; i++);
    }
}

void ktos_DebugPrintf(const char *fmt, ...) { (void)fmt; }

void ktos_InitSys(void)
{
    ktos_hal_InitSystemTimer(NULL);
    sei();
}

WORD blink_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam;
    (void)lParam;

    if (MsgType == KTOS_MSG_TYPE_INIT) {
        DDRB |= (1 << PB0);   /* PB0 as output */
    }

    PORTB ^= (1 << PB0);   /* toggle LED */
    return 500;              /* sleep 500ms → 1Hz blink */
}

int main(void)
{
    ktos_InitTask(blink_task, TASK_MAIN_STACK_SIZE, TASK_MAIN_QUEUE_SIZE, 'B');
    ktos_RunOS();
    return 0;
}
