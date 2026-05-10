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

/*
 * ktos_bsp_asm.S — CALL0 ABI assembly for ESP8266 (Xtensa LX106)
 *
 * Contains the three functions that must be pure assembly:
 *   ktos_task_launch      — new-task trampoline
 *   ktos_hal_ContextSwitch — full context save/restore
 *   ktos_hal_StartScheduler — first-task launch (restore only)
 *
 * Assemble with: xtensa-lx106-elf-gcc -mlongcalls -c ktos_bsp_asm.S
 * No -mabi=call0 flag needed — assembly has no ABI assumptions.
 */

    .text
    .align  4

/* =========================================================================
 * ktos_task_launch
 *
 * One-time trampoline reached via ret (= jx a0) from ktos_hal_StartScheduler
 * or ktos_hal_ContextSwitch when a new task is first scheduled.
 *
 * Register state on entry (set by InitTaskStack / ContextSwitch restore):
 *   a5 = task_func_addr
 *   a6 = task_exit_handler_addr
 *   a2/a3/a4 = initial_msg_type / initial_sparam / initial_lparam
 *
 * Sets a0 = exit_handler so the task "returns to" it, then jumps to task.
 * ========================================================================= */
    .global ktos_task_launch
    .type   ktos_task_launch, @function
ktos_task_launch:
    mov     a0, a6          /* a0 = exit handler (task's return address) */
    jx      a5              /* jump to task_func_addr                    */
    .size   ktos_task_launch, . - ktos_task_launch


/* =========================================================================
 * ktos_hal_ContextSwitch(void **p_current_sp_storage, const void *next_sp)
 *
 * CALL0 convention: a2 = p_current_sp_storage, a3 = next_sp
 *
 * Saves a0-a15 + SAR (64 bytes) onto the current stack, stores the new SP
 * into *p_current_sp_storage, loads next_sp, restores registers, and
 * returns (ret = jx a0) into the next task.
 * ========================================================================= */
    .global ktos_hal_ContextSwitch
    .type   ktos_hal_ContextSwitch, @function
ktos_hal_ContextSwitch:
    /* --- save current context --- */
    addi    a1,  a1, -64
    s32i    a0,  a1,  0         /* return address (resume PC)   */
    rsr     a0,  SAR
    s32i    a0,  a1,  4         /* SAR                          */
    s32i    a2,  a1,  8
    s32i    a3,  a1, 12
    s32i    a4,  a1, 16
    s32i    a5,  a1, 20
    s32i    a6,  a1, 24
    s32i    a7,  a1, 28
    s32i    a8,  a1, 32
    s32i    a9,  a1, 36
    s32i    a10, a1, 40
    s32i    a11, a1, 44
    s32i    a12, a1, 48
    s32i    a13, a1, 52
    s32i    a14, a1, 56
    s32i    a15, a1, 60
    /* store new SP into *p_current_sp_storage (a2 still valid) */
    s32i    a1,  a2,  0
    /* --- switch to next stack (a3 = next_sp) --- */
    mov     a1,  a3
    /* --- restore next context --- */
    l32i    a15, a1, 60
    l32i    a14, a1, 56
    l32i    a13, a1, 52
    l32i    a12, a1, 48
    l32i    a11, a1, 44
    l32i    a10, a1, 40
    l32i    a9,  a1, 36
    l32i    a8,  a1, 32
    l32i    a7,  a1, 28
    l32i    a6,  a1, 24
    l32i    a5,  a1, 20
    l32i    a4,  a1, 16
    l32i    a3,  a1, 12
    l32i    a2,  a1,  8
    l32i    a0,  a1,  4         /* a0 = SAR value               */
    wsr     a0,  SAR
    l32i    a0,  a1,  0         /* a0 = resume PC               */
    addi    a1,  a1,  64
    ret                         /* jx a0 — resume next task     */
    .size   ktos_hal_ContextSwitch, . - ktos_hal_ContextSwitch


/* =========================================================================
 * ktos_hal_StartScheduler(const void *first_task_sp)
 *
 * CALL0 convention: a2 = first_task_sp
 *
 * Restore-only half of ContextSwitch — no save because there is no current
 * task yet.  ret jumps to ktos_task_launch, which sets a0 = exit_handler
 * then jx a5 (task entry).  Never returns.
 * ========================================================================= */
    .global ktos_hal_StartScheduler
    .type   ktos_hal_StartScheduler, @function
ktos_hal_StartScheduler:
    mov     a1,  a2             /* a2 = first_task_sp           */
    l32i    a15, a1, 60
    l32i    a14, a1, 56
    l32i    a13, a1, 52
    l32i    a12, a1, 48
    l32i    a11, a1, 44
    l32i    a10, a1, 40
    l32i    a9,  a1, 36
    l32i    a8,  a1, 32
    l32i    a7,  a1, 28
    l32i    a6,  a1, 24         /* a6 = task_exit_handler_addr  */
    l32i    a5,  a1, 20         /* a5 = task_func_addr          */
    l32i    a4,  a1, 16
    l32i    a3,  a1, 12
    l32i    a2,  a1,  8
    l32i    a0,  a1,  4
    wsr     a0,  SAR
    l32i    a0,  a1,  0         /* a0 = ktos_task_launch addr   */
    addi    a1,  a1,  64
    ret                         /* → ktos_task_launch → task    */
    .size   ktos_hal_StartScheduler, . - ktos_hal_StartScheduler
