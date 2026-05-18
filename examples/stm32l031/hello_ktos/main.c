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
 * @brief KTOS Hello example for STM32L031 (ARM Cortex-M0+).
 *
 * Prints "Hello from KTOS!" to USART2 once per second.
 * Uses direct register access — no HAL or CMSIS dependency.
 * Target clock: 4 MHz MSI default (no PLL).
 *
 * Expected serial output (9600 8N1 on USART2 / PA2):
 * @code
 * ================================
 *   KTOS on STM32L031 - running!
 * ================================
 * Hello from KTOS!
 * Hello from KTOS!
 * ...
 * @endcode
 */

#include "../../../core/ktos.h"
#include "../../../core/ktos_hal.h"
#include <stdint.h>

extern void ktos_timer_irq_handler(void);

/* =========================================================================
 * RCC and GPIO registers
 * ========================================================================= */

#define RCC_IOPENR   (*(volatile uint32_t *)0x4002102CUL) /* GPIO clock enable */
#define RCC_APB1ENR  (*(volatile uint32_t *)0x40021038UL) /* APB1 clock enable */

#define GPIOA_MODER  (*(volatile uint32_t *)0x50000000UL)
#define GPIOA_AFRL   (*(volatile uint32_t *)0x50000020UL)

/* =========================================================================
 * USART2 registers (base = 0x40004400)
 * At 4 MHz MSI, 9600 baud: BRR = 4000000 / 9600 = 417
 * ========================================================================= */

#define USART2_CR1   (*(volatile uint32_t *)0x40004400UL)
#define USART2_BRR   (*(volatile uint32_t *)0x40004404UL)
#define USART2_ISR   (*(volatile uint32_t *)0x4000441CUL)
#define USART2_TDR   (*(volatile uint32_t *)0x40004428UL)

static void uart_init(void)
{
    /* Enable GPIOA clock (bit 0 of RCC_IOPENR) */
    RCC_IOPENR |= (1UL << 0);

    /* Enable USART2 clock (bit 17 of RCC_APB1ENR) */
    RCC_APB1ENR |= (1UL << 17);

    /* PA2 = AF mode: MODER bits[5:4] = 10 */
    GPIOA_MODER = (GPIOA_MODER & ~(3UL << 4)) | (2UL << 4);

    /* PA2 = AF4 (USART2_TX on STM32L031): AFRL bits[11:8] = 0100 */
    GPIOA_AFRL = (GPIOA_AFRL & ~(0xFUL << 8)) | (4UL << 8);

    /* Configure USART2: 9600 baud at 4 MHz MSI */
    USART2_BRR = 417UL;
    USART2_CR1 = (1UL << 3) | (1UL << 0); /* TE=1, UE=1 */
}

static void uart_putc(char c)
{
    while (!(USART2_ISR & (1UL << 7))); /* TXE */
    USART2_TDR = (uint32_t)(uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s) uart_putc(*s++);
}

/* =========================================================================
 * SysTick ISR — feeds the KTOS 1 ms tick
 * ========================================================================= */

void SysTick_Handler(void) { ktos_timer_irq_handler(); }

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
    __asm volatile ("cpsie i");
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
        uart_puts("  KTOS on STM32L031 - running!\r\n");
        uart_puts("================================\r\n");
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
    ktos_InitTask(hello_task, TASK_MAIN_STACK_SIZE, TASK_MAIN_QUEUE_SIZE, 'H');
    ktos_RunOS();
    return 0;
}
