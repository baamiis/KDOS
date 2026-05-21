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
 * @file ktos.c
 * @brief KTOS cooperative scheduler implementation.
 *
 * This file contains the kernel internals: task initialisation, the
 * round-robin scheduler (ktos_SwitchTask), the 1 ms timer ISR, message
 * queuing, and cooperative sleep/wake.  Application code should not call
 * any of the static functions declared here — use the public API in ktos.h.
 *
 * @ingroup ktos_core
 *
 * @warning Using ktos_Sleep() with @c HALT_TASK_SWITCH and @c KTOS_MSG_SLEEP_INDEFINITLY:
 *
 * When @c HALT_TASK_SWITCH is passed to ktos_Sleep(), the scheduler
 * focuses exclusively on the current task and will not advance to others.
 * If @c KTOS_MSG_SLEEP_INDEFINITLY is also passed, the system **halts for all other tasks**
 * until an ISR calls ktos_WakeUp() on this specific task.
 *
 * The 1 ms timer ISR (ktos_timer_irq_handler) continues to fire and update
 * task timers, but no other task will be dispatched until this task resumes.
 *
 * Use only for very short, critical sections where an ISR is guaranteed to
 * call ktos_WakeUp().  Improper use leads to an unresponsive system.
 */

#include "ktos_multi.h"
#include "ktos.h"
#include "ktos_hal.h"
#include <stdlib.h>
#include <stdbool.h>

/* =========================================================================
 * Forward declarations
 * ========================================================================= */

#ifdef __arm__
void ktos_timer_irq_handler(void) __attribute__((interrupt("IRQ")));
#else
void ktos_timer_irq_handler(void);
#endif

static void ktos_SwitchTask(void);
static void ktos_DefaultTaskExitHandler(WORD task_return_value);

/* =========================================================================
 * Module-level state
 * ========================================================================= */

/** Pointer to the task currently being dispatched (or most recently run). */
static struct ktos_TASK *TaskCurrent = NULL;

/** When @c TRUE the scheduler advances to the next task on each tick.
 *  Set to @c FALSE by ktos_Sleep() with @c HALT_TASK_SWITCH. */
static bool AllowTaskSwitch = TRUE;

/** OS scheduler stack pointer.  Set by ktos_hal_StartScheduler() and used
 *  by ktos_SwitchTask() and ktos_DefaultTaskExitHandler() to return to the
 *  scheduler context after a task yields. */
static int32_t *OS_SP = NULL;

/** Set by ktos_ExitOS() to request a clean return from ktos_RunOS(). */
static volatile bool ktos_exit_requested;

/** Holds the WORD return value of the most recently completed task function.
 *  Written by ktos_DefaultTaskExitHandler() before context-switching back to
 *  the scheduler; read by ktos_SwitchTask() immediately after the switch. */
static WORD g_LastTaskReturnValue;

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

/**
 * @brief Default exit handler called when a task function returns.
 *
 * Captures the task's return value into @c g_LastTaskReturnValue, then
 * context-switches back to the OS scheduler (ktos_SwitchTask).  The
 * scheduler reads @c g_LastTaskReturnValue to determine the task's next
 * sleep duration.
 *
 * Installed automatically by ktos_InitTask() as the @c task_exit_handler_addr
 * argument to ktos_hal_InitTaskStack().  Application code never calls this.
 *
 * @param task_return_value  The value returned by the task function
 *                           (sleep duration in ms, or @c KTOS_MSG_SLEEP_INDEFINITLY).
 */
static void ktos_DefaultTaskExitHandler(WORD task_return_value)
{
#if DEBUG
    if (TaskCurrent) {
        ktos_DebugPrintf("Task '%c' exited with value %u.\n",
                         TaskCurrent->TaskID, task_return_value);
    } else {
        ktos_DebugPrintf("Unknown task exited.\n");
    }
#endif

    g_LastTaskReturnValue = task_return_value;
    TaskCurrent->ScheduleReinit = TRUE;

    /* Return to the OS scheduler context. */
    ktos_hal_ContextSwitch((void **)&(TaskCurrent->StackPointer), OS_SP);

    ktos_Emergency("ExitHandler_CtxSwitch_Failed");
}

/* =========================================================================
 * Public API
 * ========================================================================= */

struct ktos_TASK *ktos_InitTask(
    WORD (*Func)(WORD MsgType, WORD Param1, LONG Param2),
    INT  StackSize,
    INT  QueueSize,
    BYTE TaskIDVal)
{
    struct ktos_TASK *Task;
    int32_t          *Stack;

    Task = (struct ktos_TASK *)malloc(sizeof(struct ktos_TASK));
    if (Task == NULL) { ktos_Emergency("T Failed"); }

    Stack = (int32_t *)calloc(StackSize, sizeof(int32_t));
    if (Stack == NULL) { ktos_Emergency("S Failed"); }

    Task->MsgQueue = (struct ktos_MSG *)calloc(QueueSize, sizeof(struct ktos_MSG));
    if (Task->MsgQueue == NULL) { ktos_Emergency("Q Failed"); }

    Task->Func           = Func;
    Task->TaskID         = TaskIDVal;
    Task->QCapacity  = QueueSize;
    Task->StackBasePointer      = Stack;
    Task->StackBufferSize = (unsigned int)((unsigned int)StackSize * sizeof(int32_t));
    Task->ScheduleReinit    = FALSE;

    Task->StackPointer = ktos_hal_InitTaskStack(
        Stack,
        StackSize * sizeof(int32_t),
        Func,
        ktos_DefaultTaskExitHandler,
        KTOS_MSG_TYPE_INIT,
        (WORD)0,
        (LONG)0L);

    if (Task->StackPointer == NULL) { ktos_Emergency("StackInit Failed"); }

    Task->MsgQueueIn  = Task->MsgQueue;
    Task->MsgQueueOut = Task->MsgQueue;
    Task->MsgQueueEnd = Task->MsgQueue + QueueSize;
    Task->CountdownTimer       = 0;
    Task->ISRTimer   = TRUE;   /* ready for initial INIT dispatch */
    Task->TaskSleeping    = FALSE;
    Task->NumMessages    = 0;

    /* Insert into the circular task ring. */
    if (TaskCurrent == NULL) {
        Task->TaskNext = Task;
    } else {
        Task->TaskNext = TaskCurrent->TaskNext;
        TaskCurrent->TaskNext = Task;
    }
    TaskCurrent = Task;

    return Task;
}

void ktos_ExitOS(void)
{
    ktos_exit_requested = true;
}

void ktos_RunOS(void)
{
    if (TaskCurrent == NULL) {
        ktos_Emergency("ktos_RunOS: No tasks initialized prior to starting OS!");
    }
    ktos_hal_InitSystemTimer(ktos_timer_irq_handler);
    ktos_exit_requested = false;
    ktos_SwitchTask();   /* returns only when ktos_ExitOS() has been called */
}

bool ktos_SendMsg(struct ktos_TASK *Task,
                  WORD              MsgType,
                  WORD              Param1,
                  LONG              Param2)
{
    if (Task) {
        if (Task->NumMessages >= Task->QCapacity) { return false; }
        ktos_hal_DisableInterrupts();
        struct ktos_MSG *Msg = Task->MsgQueueIn;
        Msg->MsgType = MsgType;
        Msg->Param1  = Param1;
        Msg->Param2  = Param2;
        if (++Task->MsgQueueIn >= Task->MsgQueueEnd) {
            Task->MsgQueueIn = Task->MsgQueue;
        }
        ++Task->NumMessages;
        ktos_hal_EnableInterrupts();
        return true;
    }
    return false;
}

void ktos_WakeUp(struct ktos_TASK *Task, INT TaskTypeWakeUp)
{
    if (Task) {
        ktos_hal_DisableInterrupts();
        if (Task->TaskSleeping && !Task->ISRTimer) {
            Task->ISRTimer  = TRUE;
            Task->TaskTypeWakeUp = TaskTypeWakeUp;
        }
        ktos_hal_EnableInterrupts();
    }
}

/* =========================================================================
 * Scheduler
 * ========================================================================= */

/**
 * @brief Round-robin cooperative scheduler — runs on the OS stack.
 *
 * This is the heart of KTOS.  It executes in a permanent loop on the OS
 * stack pointer (@c OS_SP) and is never called directly by application code.
 * ktos_hal_StartScheduler() transfers control here for the first time;
 * ktos_DefaultTaskExitHandler() returns here after each task yields.
 *
 * ### Scheduling algorithm
 * 1. If @c AllowTaskSwitch is true, advance @c TaskCurrent to the next task in the
 *    circular ring.
 * 2. If the task is sleeping but its timer has fired, mark it ready.
 * 3. If the task is ready (not sleeping and has a message or timer event),
 *    context-switch into it.
 * 4. On return from the task, read @c g_LastTaskReturnValue and update the
 *    task's timer accordingly.
 * 5. Repeat forever.
 */
static void ktos_SwitchTask(void)
{
    static WORD Delay;

    while (TRUE)
    {
        ktos_hal_DisableInterrupts();

        if (ktos_exit_requested) {
            ktos_hal_EnableInterrupts();
            return;
        }

        if (AllowTaskSwitch) {
            TaskCurrent = TaskCurrent->TaskNext;
        }

        if (TaskCurrent->TaskSleeping) {
            if (TaskCurrent->ISRTimer) {
                /* Sleep timer expired — mark the task ready. */
                TaskCurrent->CountdownTimer     = 0;
                TaskCurrent->ISRTimer = FALSE;
                TaskCurrent->TaskSleeping  = FALSE;
            }
        }

        if (!TaskCurrent->TaskSleeping &&
            (TaskCurrent->NumMessages != 0 || TaskCurrent->ISRTimer))
        {
            if (TaskCurrent->ScheduleReinit) {
                /*
                 * Handler model: task previously returned a value.
                 * Re-initialise the stack with the next pending message so
                 * the task is called fresh as task_func(MsgType, Param1, Param2).
                 */
                WORD msg_type = KTOS_MSG_TYPE_TIMER;
                WORD s_param  = 0;
                LONG l_param  = 0;

                if (TaskCurrent->NumMessages != 0) {
                    /* Read message content before consuming. */
                    msg_type = TaskCurrent->MsgQueueOut->MsgType;
                    s_param  = TaskCurrent->MsgQueueOut->Param1;
                    l_param  = TaskCurrent->MsgQueueOut->Param2;
                    if (++TaskCurrent->MsgQueueOut >= TaskCurrent->MsgQueueEnd) {
                        TaskCurrent->MsgQueueOut = TaskCurrent->MsgQueue;
                    }
                    --TaskCurrent->NumMessages;
                }

                TaskCurrent->ScheduleReinit = FALSE;
                TaskCurrent->StackPointer = (int32_t *)ktos_hal_InitTaskStack(
                    TaskCurrent->StackBasePointer,
                    TaskCurrent->StackBufferSize,
                    TaskCurrent->Func,
                    ktos_DefaultTaskExitHandler,
                    msg_type, s_param, l_param);
            }
            /* else: coroutine model (ktos_Sleep) or fresh first-run — restore
             * existing StackPointer as-is; do not consume from the message queue. */

            /* Switch into the task; returns when the task yields back. */
            ktos_hal_ContextSwitch((void **)&OS_SP, TaskCurrent->StackPointer);
            /* Interrupts assumed disabled on return. */

            Delay = g_LastTaskReturnValue;

            if (Delay == 0) {
                /* Yield — re-schedule immediately. */
                TaskCurrent->ISRTimer = TRUE;
                TaskCurrent->CountdownTimer     = 0;
            } else if (Delay == KTOS_MSG_SLEEP_INDEFINITLY) {
                /* Sleep indefinitely until a message arrives. */
                TaskCurrent->CountdownTimer     = 0;
                TaskCurrent->ISRTimer = FALSE;
            } else {
                /* Sleep for Delay milliseconds. */
                TaskCurrent->CountdownTimer     = Delay;
                TaskCurrent->ISRTimer = FALSE;
            }
        }

        ktos_hal_EnableInterrupts();
    }
}

INT ktos_Sleep(WORD Delay, bool TaskAllowSwitch)
{
    ktos_hal_DisableInterrupts();

    TaskCurrent->TaskSleeping  = TRUE;
    TaskCurrent->TaskTypeWakeUp = 0;

    if (Delay == 0) {
        TaskCurrent->ISRTimer = TRUE;
        TaskCurrent->CountdownTimer     = 0;
    } else if (Delay == KTOS_MSG_SLEEP_INDEFINITLY) {
        TaskCurrent->CountdownTimer = 0;
    } else {
        TaskCurrent->CountdownTimer = Delay;
    }

    AllowTaskSwitch = TaskAllowSwitch;

    ktos_hal_ContextSwitch((void **)&(TaskCurrent->StackPointer), OS_SP);

    AllowTaskSwitch = TRUE;
    ktos_hal_EnableInterrupts();
    return TaskCurrent->TaskTypeWakeUp;
}

/* =========================================================================
 * 1 ms timer ISR
 * ========================================================================= */

/**
 * @brief 1 ms system tick interrupt handler.
 *
 * Called by the BSP's hardware timer ISR every millisecond.
 * Walks the circular task ring and decrements each task's @c CountdownTimer counter.
 * When a counter reaches zero, @c ISRTimer is set so the scheduler
 * dispatches the task on the next scheduling cycle.
 *
 * @note This function is registered with the BSP via ktos_hal_InitSystemTimer()
 *       and must not be called directly from application code.
 */
void ktos_timer_irq_handler(void)
{
    struct ktos_TASK *Task = TaskCurrent;
    if (!Task) return;
    do {
        if (Task->CountdownTimer) {
            if (--Task->CountdownTimer == 0) {
                Task->ISRTimer = TRUE;
            }
        }
        Task = Task->TaskNext;
    } while (Task != TaskCurrent);
}
