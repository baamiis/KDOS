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
 * @file ktos_bsp.c
 * @brief KTOS Board Support Package — ESP8266 (Xtensa LX106) — partial
 *
 * Supported boards: NodeMCU, Wemos D1 Mini, ESP-01.
 *
 * | Property  | Value                              |
 * |-----------|------------------------------------|
 * | Core      | Xtensa LX106 32-bit               |
 * | RAM       | ~30 KB free (after WiFi stack)    |
 * | Flash     | 1 MB+                             |
 * | Clock     | 80 MHz                            |
 * | Toolchain | xtensa-lx106-elf-gcc (ESP8266 SDK)|
 *
 * ### Build
 * @code
 * xtensa-lx106-elf-gcc -mlongcalls ...
 * @endcode
 *
 * ### Status
 * - C parts (interrupts, timer, stack init): **complete**
 * - Assembly (ContextSwitch, StartScheduler): **complete**
 *
 * ### Build flag required
 * This BSP file **must** be compiled with @c -mabi=call0 to disable Xtensa
 * register windowing.  Add to your Makefile / SDK build flags:
 * @code
 * -mabi=call0 -mno-serialize-volatile
 * @endcode
 * Without @c -mabi=call0 the context switch will silently corrupt state
 * because ENTRY/RETW window rotation is not handled.
 *
 * ### Context frame layout (64 bytes, 16-byte aligned, grows downward)
 * | Offset | Register | Notes                                              |
 * |--------|----------|----------------------------------------------------|
 * | +0     | a0       | Resume PC.  New tasks: @c ktos_task_launch addr.  |
 * | +4     | SAR      | Shift-amount register.                             |
 * | +8     | a2       | New tasks: initial_msg_type argument.              |
 * | +12    | a3       | New tasks: initial_sparam argument.                |
 * | +16    | a4       | New tasks: initial_lparam argument.                |
 * | +20    | a5       | New tasks: task_func_addr.                         |
 * | +24    | a6       | New tasks: task_exit_handler_addr.                 |
 * | +28–60 | a7–a15   | Zeroed for new tasks.                              |
 *
 * @defgroup ktos_bsp_xtensa KTOS BSP — Xtensa LX106 (ESP8266)
 * @ingroup  ktos_hal
 */

#include "../../core/ktos_hal.h"
#include <stdint.h>

/* =========================================================================
 * Interrupt control
 * ========================================================================= */

/** @brief Disable all interrupts (Xtensa @c RSIL a2, 15 — set level 15). */
void ktos_hal_DisableInterrupts(void)
{
    __asm volatile ("rsil a2, 15" ::: "a2", "memory");
}

/** @brief Re-enable interrupts — set PS.INTLEVEL to 0 (unmask all levels).
 *
 * Safe after startup.c has called ets_wdt_disable() + system_soft_wdt_stop()
 * and ktos_hal_InitSystemTimer() has stopped FRC1.  Those were the only ROM
 * timer sources that fired spurious callbacks into the UART TX path.
 *
 * Allowing interrupts lets the WiFi MAC's hardware beacon timer fire, so a
 * soft-AP stays visible to scanners and a station connection is maintained
 * at the hardware level.  Higher-level SDK events (auth, DHCP) still require
 * ets_run() — they queue up but are not processed. */
void ktos_hal_EnableInterrupts(void)
{
    __asm volatile ("rsil a2, 0" ::: "a2", "memory");
}

/* =========================================================================
 * System timer — FRC1, 1 ms tick at 80 MHz
 * ========================================================================= */

#define FRC1_LOAD_ADDRESS   0x60000600UL
#define FRC1_COUNT_ADDRESS  0x60000604UL
#define FRC1_CTRL_ADDRESS   0x60000608UL
#define FRC1_INT_ADDRESS    0x6000060CUL

#define FRC1_CTRL_DIV_256   (3 << 2)
#define FRC1_CTRL_RELOAD    (1 << 6)
#define FRC1_CTRL_INT_EN    (1 << 7)
#define FRC1_TICKS_PER_MS   ((80000000UL / 256) / 1000)  /* ~312 at 80MHz */

/**
 * @brief System timer initialisation — intentional no-op on ESP8266.
 *
 * FRC1 management is left to each example's startup.c:
 *
 * - Examples that abandon ets_run() (wifi_scan, wifi_connect) stop FRC1
 *   in startup.c BEFORE the ABI crossing so the SDK's os_timer ISR cannot
 *   corrupt UART output once ets_run() is gone.
 *
 * - Examples that return to ets_run() after KTOS finishes (wifi_AP) leave
 *   FRC1 running so the SDK's os_timer callbacks continue servicing the
 *   WiFi stack (beacon management, DHCP server, etc.).
 *
 * KTOS timer-based task wakeups (ktos_Sleep with ms > 0) are not used in
 * any current ESP8266 example, so the ktos_timer_irq_handler is not hooked.
 */
void ktos_hal_InitSystemTimer(void (*timer_isr_addr)(void))
{
    (void)timer_isr_addr;
}

/* =========================================================================
 * Task-launch trampoline
 * ========================================================================= */

/* Implemented in ktos_bsp_asm.S — pure assembly, no compiler prologue/epilogue. */
extern void ktos_task_launch(void);

/* =========================================================================
 * Task stack initialisation
 * ========================================================================= */

/**
 * @brief Build the initial CALL0 context frame for a new task.
 *
 * Fills in a 64-byte frame (16 words) that is indistinguishable from a frame
 * pushed by ktos_hal_ContextSwitch(), so the scheduler can switch to a new
 * task the same way it switches to a running one.
 *
 * When this frame is first restored:
 * - @c ret (= @c jx @c a0) jumps to ktos_task_launch
 * - ktos_task_launch moves a6 → a0 (exit handler) then @c jx @c a5 (task entry)
 * - The task runs with a2=MsgType, a3=sParam, a4=lParam, a0=exit_handler
 */
void *ktos_hal_InitTaskStack(void *p_stack_base,
                              unsigned int stack_size_bytes,
                              WORD (*task_func_addr)(WORD, WORD, LONG),
                              void (*task_exit_handler_addr)(WORD),
                              WORD initial_msg_type,
                              WORD initial_sparam,
                              LONG initial_lparam)
{
    uint32_t *sp = (uint32_t *)((uint8_t *)p_stack_base + stack_size_bytes);
    sp = (uint32_t *)((uint32_t)sp & ~0xFUL); /* 16-byte alignment */
    sp -= 16;                                  /* 16 words = 64-byte frame */

    sp[0]  = (uint32_t)ktos_task_launch;        /* a0  — resume PC (trampoline)  */
    sp[1]  = 0;                                 /* SAR — zeroed                  */
    sp[2]  = (uint32_t)initial_msg_type;        /* a2  — first task argument     */
    sp[3]  = (uint32_t)initial_sparam;          /* a3  — second task argument    */
    sp[4]  = (uint32_t)initial_lparam;          /* a4  — third task argument     */
    sp[5]  = (uint32_t)task_func_addr;          /* a5  — read by trampoline      */
    sp[6]  = (uint32_t)task_exit_handler_addr;  /* a6  — read by trampoline      */
    sp[7]  = 0; sp[8]  = 0; sp[9]  = 0;        /* a7-a9                         */
    sp[10] = 0; sp[11] = 0; sp[12] = 0;        /* a10-a12                       */
    sp[13] = 0; sp[14] = 0; sp[15] = 0;        /* a13-a15                       */

    return (void *)sp;
}

/* =========================================================================
 * Context switch and scheduler launch — CALL0 ABI assembly
 * ========================================================================= */

/**
 * @brief CALL0 cooperative context switch.
 *
 * Saves a0–a15 and SAR onto the current task's stack (64 bytes),
 * records the new SP in @c *p_current_sp_storage, loads @p next_sp,
 * restores the new context, and returns (@c ret = @c jx @c a0) into it.
 *
 * For a **running** task the saved a0 is the return address back into the
 * task code that called ContextSwitch.  For a **new** task a0 holds the
 * address of ktos_task_launch(), which finishes argument setup before
 * jumping to the real task function.
 *
 * @note The caller must disable interrupts before calling this function.
 *       Compile this file with @c -mabi=call0.
 */
/* Implemented in ktos_bsp_asm.S */
void ktos_hal_ContextSwitch(void **p_current_sp_storage, const void *next_sp);

/**
 * @brief Load the first task and start executing it.  Never returns.
 *
 * Identical to the restore half of ktos_hal_ContextSwitch(); there is no
 * "save" because there is no current task yet.  @c ret jumps to
 * ktos_task_launch(), which moves @c a6 → @c a0 (exit handler) and then
 * @c jx @c a5 (task entry), entering the task with the correct arguments
 * and a proper return address.
 *
 * @note Compile with @c -mabi=call0.
 */
/* Implemented in ktos_bsp_asm.S */
void ktos_hal_StartScheduler(const void *first_task_sp);
