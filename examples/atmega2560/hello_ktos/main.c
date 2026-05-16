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
 * @brief KTOS Hello example for ATmega2560 (AVR 8-bit).
 *
 * Prints "Hello from KTOS!" to UART0 once per second.
 * Uses direct AVR register access — no Arduino library dependency.
 *
 * Expected serial output (9600 8N1 on UART0):
 * @code
 * ================================
 *   KTOS on ATmega2560 - running!
 * ================================
 * Hello from KTOS!
 * Hello from KTOS!
 * ...
 * @endcode
 */

#include "../../../core/ktos.h"
#include "../../../core/ktos_hal.h"
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>

extern void ktos_timer_irq_handler(void);

/* =========================================================================
 * UART0 — ATmega2560, 9600 baud at 16 MHz
 * UBRR = F_CPU / (16 * baud) - 1 = 16000000 / (16 * 9600) - 1 = 103
 * ========================================================================= */

static void uart_init(void)
{
    UBRR0H = 0;
    UBRR0L = 103;                            /* 9600 baud at 16MHz */
    UCSR0B = (1 << TXEN0);
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00); /* 8-bit, 1 stop, no parity */
}

static void uart_putc(char c)
{
    while (!(UCSR0A & (1 << UDRE0)));
    UDR0 = (uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s) uart_putc(*s++);
}

/* =========================================================================
 * Timer1 ISR — feeds the KTOS 1 ms tick
 * ========================================================================= */

ISR(TIMER1_COMPA_vect) { ktos_timer_irq_handler(); }

/* =========================================================================
 * Required KTOS platform callbacks
 * ========================================================================= */

static uint8_t g_uart_ready = 0;

__attribute__((noreturn)) void ktos_Emergency(const char *msg)
{
    if (g_uart_ready) {
        uart_puts("\r\n[KTOS FATAL] ");
        uart_puts(msg);
        uart_puts("\r\n");
    }
    for (;;);
}

void ktos_DebugPrintf(const char *fmt, ...) { (void)fmt; }

void ktos_InitSys(void)
{
    ktos_hal_InitSystemTimer(NULL);
    sei();
}

/* =========================================================================
 * Hello task
 * ========================================================================= */

WORD hello_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam;
    (void)lParam;

    if (MsgType == KTOS_MSG_TYPE_INIT) {
        uart_init();
        g_uart_ready = 1;
        uart_puts("================================\r\n");
        uart_puts("  KTOS on ATmega2560 - running!\r\n");
        uart_puts("================================\r\n");
        return 1000; /* sleep 1 second before first print */
    }

    uart_puts("Hello from KTOS!\r\n");
    return 1000; /* sleep 1 second */
}

/* =========================================================================
 * Entry point
 * ========================================================================= */

int main(void)
{
    ktos_InitTask(hello_task, TASK_MAIN_STACK_SIZE, TASK_MAIN_QUEUE_SIZE, 'H');
    ktos_RunOS();
    return 0;
}
