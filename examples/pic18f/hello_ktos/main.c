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
 * @brief KTOS Hello example for PIC18F4550 (PIC18 8-bit) [Experimental].
 *
 * Prints "Hello from KTOS!" to EUSART once per second.
 * EUSART TX on RC6, 9600 baud at 48 MHz USB PLL (instruction clock = 12 MHz).
 * SPBRG = (Fosc/(16*Baud)) - 1 = (48000000/(16*9600)) - 1 = 311.
 *
 * @warning This example requires the experimental PIC18F BSP.
 *          See bsp/pic18f/ktos_bsp.c for hardware stack limitations.
 *
 * Toolchain: SDCC with --use-non-free.
 */

#include "../../../core/ktos.h"
#if defined(__SDCC)
#include <pic18fregs.h>
#else
#include <xc.h>
#endif
#include <stdint.h>

/* =========================================================================
 * EUSART TX — RC6, 9600 baud, BRGH=1 (high speed), 48MHz Fosc
 * SPBRG = (Fosc / (16 * Baud)) - 1 = (48000000 / (16 * 9600)) - 1 = 311
 * ========================================================================= */

static void uart_init(void)
{
    /* Configure RC6 as output for TX */
    TRISC &= ~(1 << 6);

    /* EUSART: TXSTA — BRGH=1, TXEN=1, SYNC=0 */
    TXSTA = 0x24U; /* BRGH=1, TXEN=1 */

    /* RCSTA — SPEN=1 */
    RCSTA = 0x80U; /* SPEN=1 */

    /* Baud rate: SPBRG = 311, BAUDCON BRGH16=0 */
    SPBRG  = 311U & 0xFFU;
    SPBRGH = (311U >> 8) & 0xFFU;
    BAUDCON = 0x00U;
}

static void uart_putc(char c)
{
    while (!(TXSTA & (1 << 1))); /* wait for TRMT (transmit shift register empty) */
    TXREG = (uint8_t)c;
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
    ktos_hal_EnableInterrupts();
}

/* =========================================================================
 * Hello task
 * ========================================================================= */

WORD hello_task(WORD MsgType, WORD Param1, LONG Param2)
{
    (void)Param1;
    (void)Param2;

    if (MsgType == KTOS_MSG_TYPE_INIT) {
        uart_init();
        g_uart_ready = 1;
        uart_puts("================================\r\n");
        uart_puts("  KTOS on PIC18F4550 - running!\r\n");
        uart_puts("================================\r\n");
        return 1000;
    }

    uart_puts("Hello from KTOS!\r\n");
    return 1000;
}

/* =========================================================================
 * Entry point
 * ========================================================================= */

void main(void)
{
    ktos_InitTask(hello_task, TASK_MAIN_STACK_SIZE, TASK_MAIN_QUEUE_SIZE, 'H');
    ktos_RunOS();
}
