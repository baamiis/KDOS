/*
 * KTOS — Tiny Cooperative Task Switcher
 * Copyright (C) 2004-2025 Khalid Hamdou / BAAMIIS LIMITED
 * All rights reserved.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Contact: baamiis7@gmail.com
 */

#include "ktos_serial.h"
#include "board_hal.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* =========================================================================
 * Line buffer state
 * ========================================================================= */

static char                  g_line_buf[KTOS_SERIAL_LINE_MAX];
static uint16_t              g_line_len;
static ktos_serial_line_cb_t g_line_cb;

/* =========================================================================
 * Public API
 * ========================================================================= */

void ktos_serial_init(ktos_serial_line_cb_t cb)
{
    g_line_cb  = cb;
    g_line_len = 0;
    memset(g_line_buf, 0, sizeof g_line_buf);
}

void ktos_serial_poll(void)
{
    char c;
    while (board_serial_read_char(&c)) {
        if (c == '\r')
            continue;   /* ignore CR in CRLF sequences */

        if (c == '\n') {
            /* Complete line received — null-terminate and dispatch */
            g_line_buf[g_line_len] = '\0';
            if (g_line_len > 0 && g_line_cb)
                g_line_cb(g_line_buf);
            g_line_len = 0;
            continue;
        }

        if (g_line_len < KTOS_SERIAL_LINE_MAX - 1) {
            g_line_buf[g_line_len++] = c;
        } else {
            /* Line overflow — discard and reset */
            g_line_len = 0;
        }
    }
}

void ktos_serial_printf(const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf - 2, fmt, ap);
    va_end(ap);

    if (n > 0) {
        /* Ensure line ends with exactly one newline */
        if (buf[n - 1] != '\n') {
            buf[n]     = '\n';
            buf[n + 1] = '\0';
        }
        board_serial_write(buf);
    }
}
