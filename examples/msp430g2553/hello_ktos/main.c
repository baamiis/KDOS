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
 * @brief KTOS Hello example for MSP430G2553 (MSP430 16-bit).
 *
 * Prints "Hello from KTOS!" to USCI A0 UART once per second.
 * P1.1 = UCA0RXD, P1.2 = UCA0TXD (MSP430G2553 LaunchPad default).
 * SMCLK = 1 MHz default, 9600 baud: UCA0BR0 = 104.
 *
 * Expected serial output (9600 8N1 on P1.2):
 * @code
 * ==================================
 *   KTOS on MSP430G2553 - running!
 * ==================================
 * Hello from KTOS!
 * Hello from KTOS!
 * ...
 * @endcode
 */

#include "../../../core/ktos.h"
#include <msp430.h>
#include <stdint.h>

/* =========================================================================
 * Timer_A CCR0 ISR — feeds the KTOS 1 ms tick
 * ========================================================================= */

#pragma vector = TIMER0_A0_VECTOR
__interrupt void Timer_A_CCR0_ISR(void) { ktos_timer_irq_handler(); }

/* =========================================================================
 * USCI A0 UART — 9600 baud at 1 MHz SMCLK
 * UCA0BR0 = 104, UCA0MCTL = UCBRS0 for ~1.7% error
 * ========================================================================= */

static void uart_init(void)
{
    P1SEL  |= (1 << 1) | (1 << 2);   /* P1.1=RXD, P1.2=TXD secondary function */
    P1SEL2 |= (1 << 1) | (1 << 2);
    UCA0CTL1 |= UCSSEL_2;             /* SMCLK */
    UCA0BR0   = 104;                  /* 9600 baud at 1MHz */
    UCA0BR1   = 0;
    UCA0MCTL  = UCBRS0;
    UCA0CTL1 &= ~UCSWRST;
}

static void uart_putc(char c)
{
    while (!(IFG2 & UCA0TXIFG));
    UCA0TXBUF = (uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s) uart_putc(*s++);
}

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
    __enable_interrupt();
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
        uart_puts("==================================\r\n");
        uart_puts("  KTOS on MSP430G2553 - running!\r\n");
        uart_puts("==================================\r\n");
        return 1000;
    }

    uart_puts("Hello from KTOS!\r\n");
    return 1000;
}

/* =========================================================================
 * Entry point
 * ========================================================================= */

int main(void)
{
    WDTCTL = WDTPW | WDTHOLD;  /* Stop watchdog */
    ktos_InitTask(hello_task, TASK_MAIN_STACK_SIZE, TASK_MAIN_QUEUE_SIZE, 'H');
    ktos_RunOS();
    return 0;
}
