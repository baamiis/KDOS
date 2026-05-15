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

#include "ktos_multi.h"
#include "ktos.h"
#include <stdarg.h>
#include <stdio.h>

static WORD ktos_TaskMainProc(WORD MsgType, WORD sParam, LONG lParam);

struct ktos_TASK *TaskMain;
extern struct ktos_TASK *TaskSerial;
extern struct ktos_TASK *TaskCheckSum;

void ktos_Emergency(const char *Msg) {
  (void)Msg;
  while (1) {
  }
}

void ktos_DebugPrintf(const char *Format, ...) {
  (void)Format;
}

void ktos_InitSys(void) {
}

int main()
{
  TaskMain = ktos_InitTask(ktos_TaskMainProc,
                      TASK_MAIN_STACK_SIZE,
                      TASK_MAIN_QUEUE_SIZE,
                      TASK_MAIN_ID);
  
  if (!ktos_SendMsg(TaskMain, KTOS_MSG_TYPE_INIT, 0, 0)) {
    ktos_Emergency("MainTask_InitMsg_Failed");
  }

  ktos_RunOS();
  return 0;
}

static WORD ktos_TaskMainProc(WORD MsgType, WORD sParam, LONG lParam)
{
  (void)sParam;
  (void)lParam;

  switch(MsgType) {
    case KTOS_MSG_TYPE_INIT:
      ktos_InitSys();
      break;
    case KTOS_MSG_TYPE_SYSTEM_START:
      break;
  }
  return MSG_WAIT;
}