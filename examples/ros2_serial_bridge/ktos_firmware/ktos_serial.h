/*
 * KTOS — Tiny Cooperative Task Switcher
 * Copyright (C) 2004-2025 Khalid Hamdou / BAAMIIS LIMITED
 * All rights reserved.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Contact: baamiis7@gmail.com
 */

/**
 * @file ktos_serial.h
 * @brief Lightweight newline-delimited serial RX line buffer.
 *
 * Collects characters from board_serial_read_char() into a line buffer.
 * When a newline is received, the complete line is passed to a callback.
 * This runs inside a KTOS task — no ISR required on the RX side.
 */

#ifndef KTOS_SERIAL_H
#define KTOS_SERIAL_H

#include <stdint.h>

#define KTOS_SERIAL_LINE_MAX  64   /* max bytes in one incoming line */

/**
 * @brief Callback type invoked when a complete line has been received.
 * @param line  Null-terminated string, newline stripped.
 */
typedef void (*ktos_serial_line_cb_t)(const char *line);

/**
 * @brief Initialise the serial line buffer and register the line callback.
 * @param cb  Called once per complete line received.
 */
void ktos_serial_init(ktos_serial_line_cb_t cb);

/**
 * @brief Poll the UART RX register and accumulate characters.
 *
 * Call this frequently from the serial RX KTOS task.
 * Invokes the registered callback when a complete line is available.
 */
void ktos_serial_poll(void);

/**
 * @brief Send a formatted line to the serial port (adds \\n automatically).
 * @param fmt  printf-style format string.
 */
void ktos_serial_printf(const char *fmt, ...);

#endif /* KTOS_SERIAL_H */
