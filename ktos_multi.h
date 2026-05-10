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

#ifndef KTOS_MULTI_H_INCLUDED
#define KTOS_MULTI_H_INCLUDED

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifndef DEBUG
  #define DEBUG                    0
#endif

typedef unsigned short  WORD;
typedef long            LONG;
typedef int             INT;
typedef unsigned char   BYTE;

#define TRUE  1
#define FALSE 0

void ktos_Emergency(const char *Msg);
void ktos_DebugPrintf(const char *Format, ...);
void ktos_InitSys(void);

#endif /* KTOS_MULTI_H_INCLUDED */