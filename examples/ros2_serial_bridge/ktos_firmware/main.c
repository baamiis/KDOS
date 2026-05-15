/*
 * KTOS — Tiny Cooperative Task Switcher
 * Copyright (C) 2004-2025 Khalid Hamdou / BAAMIIS LIMITED
 * All rights reserved.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Contact: baamiis7@gmail.com
 */

/**
 * @file main.c
 * @brief Entry point for the KTOS ROS 2 Serial Bridge firmware demo.
 *
 * Registers five KTOS tasks and starts the cooperative scheduler.
 * All board-specific setup is delegated to board_init() in board_hal.c.
 *
 * Task summary:
 *   'H'  task_heartbeat  — HEARTBEAT,<n> every 1000 ms
 *   'N'  task_sensor     — fake ADC update every 500 ms
 *   'T'  task_status     — STATUS,... every 2000 ms
 *   'R'  task_serial_rx  — UART poll every 20 ms
 *   'C'  task_command    — LED/motor actuator, woken by task_serial_rx
 */

#include "ktos_app.h"
#include "board_hal.h"
#include <stdio.h>

/* =========================================================================
 * KTOS mandatory platform callbacks
 * ========================================================================= */

__attribute__((noreturn))
void ktos_Emergency(const char *msg)
{
    /* On a real board: disable interrupts, flash error LED, output UART message */
    fprintf(stderr, "[KTOS FATAL] %s\n", msg);
    while (1);
}

void ktos_DebugPrintf(const char *fmt, ...)
{
    /* Enable in debug builds by forwarding to printf or a UART debug port */
    (void)fmt;
}

void ktos_InitSys(void)
{
    /* Called once by ktos_RunOS() before the first task dispatch.
     * Leave empty — board_init() handles hardware setup before ktos_RunOS(). */
}

/* =========================================================================
 * Application entry point
 * ========================================================================= */

int main(void)
{
    board_init();

    /*
     * Register tasks.
     * Stack sizes are conservative defaults — tune down for RAM-constrained MCUs.
     *
     * ktos_InitTask(task_func, stack_words, queue_depth, task_id_char)
     *
     * TODO: Adjust TASK_MAIN_STACK_SIZE (512 words) down for small MCUs.
     *       128 words is typically sufficient for these tasks on ARM Cortex-M.
     */
    g_task_heartbeat = ktos_InitTask(task_heartbeat, TASK_MAIN_STACK_SIZE, 3, 'H');
    g_task_sensor    = ktos_InitTask(task_sensor,    TASK_MAIN_STACK_SIZE, 3, 'N');
    g_task_status    = ktos_InitTask(task_status,    TASK_MAIN_STACK_SIZE, 3, 'T');
    g_task_serial_rx = ktos_InitTask(task_serial_rx, TASK_MAIN_STACK_SIZE, 8, 'R');
    g_task_command   = ktos_InitTask(task_command,   TASK_MAIN_STACK_SIZE, 8, 'C');

    printf("[KTOS] Tasks registered — starting scheduler\n");

    ktos_RunOS();   /* never returns */

    return 0;       /* unreachable */
}
