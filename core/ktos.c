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
 * @warning Using ktos_Sleep() with @c TASK_SWITCH_INHIBIT and @c MSG_WAIT:
 *
 * When @c TASK_SWITCH_INHIBIT is passed to ktos_Sleep(), the scheduler
 * focuses exclusively on the current task and will not advance to others.
 * If @c MSG_WAIT is also passed, the system **halts for all other tasks**
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
 *  Set to @c FALSE by ktos_Sleep() with @c TASK_SWITCH_INHIBIT. */
static bool MultiTask = TRUE;

/** OS scheduler stack pointer.  Set by ktos_hal_StartScheduler() and used
 *  by ktos_SwitchTask() and ktos_DefaultTaskExitHandler() to return to the
 *  scheduler context after a task yields. */
static int32_t *OS_SP = NULL;

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
 *                           (sleep duration in ms, or @c MSG_WAIT).
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

    /* Return to the OS scheduler context. */
    ktos_hal_ContextSwitch((void **)&(TaskCurrent->StackPtr), OS_SP);

    ktos_Emergency("ExitHandler_CtxSwitch_Failed");
    while (1);
}

/* =========================================================================
 * Public API
 * ========================================================================= */

struct ktos_TASK *ktos_InitTask(
    WORD (*Func)(WORD MsgType, WORD sParam, LONG lParam),
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

    Task->Func         = Func;
    Task->TaskID       = TaskIDVal;
    Task->QueueCapacity = QueueSize;

    Task->StackPtr = ktos_hal_InitTaskStack(
        Stack,
        StackSize * sizeof(int32_t),
        Func,
        ktos_DefaultTaskExitHandler,
        KTOS_MSG_TYPE_INIT,
        (WORD)0,
        (LONG)0L);

    if (Task->StackPtr == NULL) { ktos_Emergency("StackInit Failed"); }

    Task->MsgQueueIn  = Task->MsgQueue;
    Task->MsgQueueOut = Task->MsgQueue;
    Task->MsgQueueEnd = Task->MsgQueue + QueueSize;
    Task->Timer       = 0;
    Task->TimerFlag   = FALSE;
    Task->Sleeping    = FALSE;
    Task->MsgCount    = 0;

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

void ktos_RunOS(void)
{
    if (TaskCurrent == NULL) {
        ktos_Emergency("ktos_RunOS: No tasks initialized prior to starting OS!");
        while (1);
    }
    ktos_hal_InitSystemTimer(ktos_timer_irq_handler);
    ktos_hal_StartScheduler(TaskCurrent->StackPtr);
    ktos_Emergency("ktos_RunOS: ktos_hal_StartScheduler returned unexpectedly!");
    while (1);
}

bool ktos_SendMsg(struct ktos_TASK *Task,
                  WORD              MsgType,
                  WORD              sParam,
                  LONG              lParam)
{
    if (Task) {
        if (Task->MsgCount >= Task->QueueCapacity) { return false; }
        ktos_hal_DisableInterrupts();
        struct ktos_MSG *Msg = Task->MsgQueueIn;
        Msg->MsgType = MsgType;
        Msg->sParam  = sParam;
        Msg->lParam  = lParam;
        if (++Task->MsgQueueIn >= Task->MsgQueueEnd) {
            Task->MsgQueueIn = Task->MsgQueue;
        }
        ++Task->MsgCount;
        ktos_hal_EnableInterrupts();
        return true;
    }
    return false;
}

void ktos_WakeUp(struct ktos_TASK *Task, INT WakeUpType)
{
    if (Task) {
        ktos_hal_DisableInterrupts();
        if (Task->Sleeping && !Task->TimerFlag) {
            Task->TimerFlag  = TRUE;
            Task->WakeUpType = WakeUpType;
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
 * 1. If @c MultiTask is true, advance @c TaskCurrent to the next task in the
 *    circular ring.
 * 2. If the task is sleeping but its timer has fired, mark it ready.
 * 3. If the task is ready (not sleeping and has a message or timer event),
 *    context-switch into it.
 * 4. On return from the task, read @c g_LastTaskReturnValue and update the
 *    task's timer accordingly.
 * 5. Repeat forever.
 */
static void __attribute__((unused)) ktos_SwitchTask(void)
{
    static WORD Delay;

    while (TRUE)
    {
        ktos_hal_DisableInterrupts();

        if (MultiTask) {
            TaskCurrent = TaskCurrent->TaskNext;
        }

        if (TaskCurrent->Sleeping) {
            if (TaskCurrent->TimerFlag) {
                /* Sleep timer expired — mark the task ready. */
                TaskCurrent->Timer     = 0;
                TaskCurrent->TimerFlag = FALSE;
                TaskCurrent->Sleeping  = FALSE;
            }
        }

        if (!TaskCurrent->Sleeping &&
            (TaskCurrent->MsgCount != 0 || TaskCurrent->TimerFlag))
        {
            if (TaskCurrent->MsgCount != 0) {
                /* Advance the read pointer (consume the oldest message). */
                if (++TaskCurrent->MsgQueueOut >= TaskCurrent->MsgQueueEnd) {
                    TaskCurrent->MsgQueueOut = TaskCurrent->MsgQueue;
                }
                --TaskCurrent->MsgCount;
            }

            /* Switch into the task; returns when the task yields back. */
            ktos_hal_ContextSwitch((void **)&OS_SP, TaskCurrent->StackPtr);
            /* Interrupts assumed disabled on return. */

            Delay = g_LastTaskReturnValue;

            if (Delay == 0) {
                /* Yield — re-schedule immediately. */
                TaskCurrent->TimerFlag = TRUE;
                TaskCurrent->Timer     = 0;
            } else if (Delay == MSG_WAIT) {
                /* Sleep indefinitely until ktos_WakeUp(). */
                TaskCurrent->Timer     = 0;
                TaskCurrent->TimerFlag = FALSE;
            } else {
                /* Sleep for Delay milliseconds. */
                TaskCurrent->Timer     = Delay;
                TaskCurrent->TimerFlag = FALSE;
            }
        }

        ktos_hal_EnableInterrupts();
    }
}

INT ktos_Sleep(WORD Delay, bool TaskSwitchPermit)
{
    ktos_hal_DisableInterrupts();

    TaskCurrent->Sleeping  = TRUE;
    TaskCurrent->WakeUpType = 0;

    if (Delay == 0) {
        TaskCurrent->TimerFlag = TRUE;
        TaskCurrent->Timer     = 0;
    } else if (Delay == MSG_WAIT) {
        TaskCurrent->Timer = 0;
    } else {
        TaskCurrent->Timer = Delay;
    }

    MultiTask = TaskSwitchPermit;

    ktos_hal_ContextSwitch((void **)&(TaskCurrent->StackPtr), OS_SP);

    MultiTask = TRUE;
    ktos_hal_EnableInterrupts();
    return TaskCurrent->WakeUpType;
}

/* =========================================================================
 * 1 ms timer ISR
 * ========================================================================= */

/**
 * @brief 1 ms system tick interrupt handler.
 *
 * Called by the BSP's hardware timer ISR every millisecond.
 * Walks the circular task ring and decrements each task's @c Timer counter.
 * When a counter reaches zero, @c TimerFlag is set so the scheduler
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
        if (Task->Timer) {
            if (--Task->Timer == 0) {
                Task->TimerFlag = TRUE;
            }
        }
        Task = Task->TaskNext;
    } while (Task != TaskCurrent);
}
