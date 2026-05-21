/*
 * KTOS — Tiny Cooperative Task Switcher
 * Copyright (C) 2004-2025 Khalid Hamdou / BAAMIIS LIMITED
 * All rights reserved.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Contact: baamiis7@gmail.com
 */

/**
 * @file ktos_app.c
 * @brief KTOS cooperative tasks for the ROS 2 Serial Bridge demo.
 *
 * Five tasks run cooperatively under KTOS:
 *
 *  task_heartbeat  — sends HEARTBEAT,<n>          every 1000 ms
 *  task_sensor     — updates fake sensor          every  500 ms
 *  task_status     — sends STATUS,...             every 2000 ms
 *  task_serial_rx  — polls UART and parses lines  every   20 ms
 *  task_command    — applies LED / motor commands on receipt
 *
 * The serial_rx task parses incoming lines and posts inter-task messages
 * to task_command via ktos_SendMsg().  This keeps parsing and actuation
 * decoupled so neither blocks the scheduler.
 */

#include "ktos_app.h"
#include "ktos_serial.h"
#include "board_hal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* =========================================================================
 * Shared state
 * ========================================================================= */

volatile int      g_led_state    = 0;
volatile int      g_motor_speed  = 0;
volatile int      g_sensor_value = 0;
volatile uint32_t g_uptime_ms    = 0;

/* =========================================================================
 * Task handles
 * ========================================================================= */

struct ktos_TASK *g_task_heartbeat  = NULL;
struct ktos_TASK *g_task_sensor     = NULL;
struct ktos_TASK *g_task_status     = NULL;
struct ktos_TASK *g_task_serial_rx  = NULL;
struct ktos_TASK *g_task_command    = NULL;

/* =========================================================================
 * task_heartbeat — sends HEARTBEAT,<n> every 1000 ms
 * ========================================================================= */

WORD task_heartbeat(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam; (void)lParam;

    static uint32_t counter = 0;

    if (MsgType == KTOS_MSG_TYPE_INIT)
        return 1000;   /* first wakeup in 1000 ms */

    if (MsgType == KTOS_MSG_TYPE_TIMER) {
        ktos_serial_printf("HEARTBEAT,%lu", (unsigned long)counter++);
        g_uptime_ms = board_millis();
    }

    return 1000;
}

/* =========================================================================
 * task_sensor — updates simulated sensor value every 500 ms
 * ========================================================================= */

WORD task_sensor(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam; (void)lParam;

    if (MsgType == KTOS_MSG_TYPE_INIT)
        return 500;

    if (MsgType == KTOS_MSG_TYPE_TIMER) {
        /*
         * Simulate an ADC reading that oscillates between 0 and 1023.
         * Replace with board_adc_read() or similar on real hardware.
         */
        static int direction = 1;
        g_sensor_value += direction * 13;
        if (g_sensor_value >= 1023) { g_sensor_value = 1023; direction = -1; }
        if (g_sensor_value <= 0)    { g_sensor_value = 0;    direction =  1; }
    }

    return 500;
}

/* =========================================================================
 * task_status — sends STATUS,... every 2000 ms
 * ========================================================================= */

WORD task_status(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam; (void)lParam;

    if (MsgType == KTOS_MSG_TYPE_INIT)
        return 2000;

    if (MsgType == KTOS_MSG_TYPE_TIMER) {
        ktos_serial_printf("STATUS,%lu,%d,%d,%d",
            (unsigned long)board_millis(),
            (int)g_led_state,
            (int)g_motor_speed,
            (int)g_sensor_value);
    }

    return 2000;
}

/* =========================================================================
 * Serial line parser — called by ktos_serial_poll() on each complete line
 * ========================================================================= */

static void on_serial_line(const char *line)
{
    if (strcmp(line, "PING") == 0) {
        ktos_serial_printf("PONG");
        return;
    }

    if (strcmp(line, "LED,ON") == 0) {
        /* TODO: Replace with actual KTOS task message send call */
        ktos_SendMsg(g_task_command, MSG_CMD_LED_ON, 0, 0);
        return;
    }

    if (strcmp(line, "LED,OFF") == 0) {
        ktos_SendMsg(g_task_command, MSG_CMD_LED_OFF, 0, 0);
        return;
    }

    if (strncmp(line, "MOTOR,", 6) == 0) {
        int speed = atoi(line + 6);
        /* sParam carries the speed (fits in a WORD as signed via cast) */
        ktos_SendMsg(g_task_command, MSG_CMD_MOTOR, (WORD)(short)speed, 0);
        return;
    }

    ktos_serial_printf("ERROR,UNKNOWN_CMD");
}

/* =========================================================================
 * task_serial_rx — polls UART RX every 20 ms
 * ========================================================================= */

WORD task_serial_rx(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam; (void)lParam;

    if (MsgType == KTOS_MSG_TYPE_INIT) {
        ktos_serial_init(on_serial_line);
        return 20;
    }

    if (MsgType == KTOS_MSG_TYPE_TIMER)
        ktos_serial_poll();

    return 20;
}

/* =========================================================================
 * task_command — applies LED / motor commands
 * ========================================================================= */

WORD task_command(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)lParam;

    if (MsgType == KTOS_MSG_TYPE_INIT)
        return KTOS_MSG_SLEEP_INDEFINITLY;   /* sleep until a command message arrives */

    switch ((AppMsgType)MsgType) {
        case MSG_CMD_LED_ON:
            g_led_state = 1;
            board_led_set(1);
            break;

        case MSG_CMD_LED_OFF:
            g_led_state = 0;
            board_led_set(0);
            break;

        case MSG_CMD_MOTOR: {
            int speed = (int)(short)sParam;   /* recover signed value */
            g_motor_speed = speed;
            board_motor_set(speed);
            break;
        }

        case MSG_CMD_PING:
            ktos_serial_printf("PONG");
            break;

        default:
            ktos_serial_printf("ERROR,UNKNOWN_MSG");
            break;
    }

    return KTOS_MSG_SLEEP_INDEFINITLY;   /* sleep again until next message */
}
