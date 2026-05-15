/*
 * KTOS — Tiny Cooperative Task Switcher
 * Copyright (C) 2004-2025 Khalid Hamdou / BAAMIIS LIMITED
 * All rights reserved.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Contact: baamiis7@gmail.com
 */

/**
 * @file ktos_app.h
 * @brief Application task declarations for the KTOS ROS 2 Serial Bridge demo.
 *
 * Each function below is a KTOS task conforming to the signature:
 *   WORD task_name(WORD MsgType, WORD sParam, LONG lParam)
 *
 * Register all tasks in main() via ktos_InitTask() before calling ktos_RunOS().
 */

#ifndef KTOS_APP_H
#define KTOS_APP_H

#include "../../../core/ktos.h"  /* WORD, LONG, ktos_InitTask, ktos_RunOS */

/* =========================================================================
 * User message types (extend KTOS_MSG_TYPE_SYSTEM_START)
 * ========================================================================= */

typedef enum {
    MSG_CMD_LED_ON    = KTOS_MSG_TYPE_SYSTEM_START + 1,
    MSG_CMD_LED_OFF,
    MSG_CMD_MOTOR,
    MSG_CMD_PING,
} AppMsgType;

/* =========================================================================
 * Shared application state (read by status task, written by command task)
 * ========================================================================= */

extern volatile int      g_led_state;
extern volatile int      g_motor_speed;
extern volatile int      g_sensor_value;
extern volatile uint32_t g_uptime_ms;

/* =========================================================================
 * Task handles — used to send inter-task messages
 * ========================================================================= */

extern struct ktos_TASK *g_task_heartbeat;
extern struct ktos_TASK *g_task_sensor;
extern struct ktos_TASK *g_task_status;
extern struct ktos_TASK *g_task_serial_rx;
extern struct ktos_TASK *g_task_command;

/* =========================================================================
 * KTOS task functions
 * ========================================================================= */

/** Sends HEARTBEAT,<n> every 1000 ms. */
WORD task_heartbeat(WORD MsgType, WORD sParam, LONG lParam);

/** Updates fake sensor value every 500 ms. */
WORD task_sensor(WORD MsgType, WORD sParam, LONG lParam);

/** Sends STATUS,... every 2000 ms. */
WORD task_status(WORD MsgType, WORD sParam, LONG lParam);

/** Polls serial RX and dispatches parsed commands. */
WORD task_serial_rx(WORD MsgType, WORD sParam, LONG lParam);

/** Applies LED / motor commands received from the serial RX task. */
WORD task_command(WORD MsgType, WORD sParam, LONG lParam);

/* =========================================================================
 * KTOS mandatory platform callbacks (implemented in main.c)
 * ========================================================================= */

void ktos_Emergency(const char *msg);
void ktos_DebugPrintf(const char *fmt, ...);
void ktos_InitSys(void);

#endif /* KTOS_APP_H */
