/*
 * WARNING: Using ktos_Sleep() with TASK_SWITCH_INHIBIT and a Delay of MSG_WAIT:
 *
 * When TASK_SWITCH_INHIBIT is used with the ktos_Sleep() function, the KTOS scheduler
 * (`ktos_SwitchTask`) will cease its normal round-robin scheduling and will focus
 * exclusively on the calling task. It will not advance to other tasks.
 *
 * If the Delay parameter is also set to MSG_WAIT (signifying an indefinite sleep),
 * the system will effectively HALT with respect to all other task activities.
 * The scheduler will continuously check only the current task, which is waiting
 * indefinitely to be woken by an explicit call to ktos_WakeUp().
 *
 * Only an Interrupt Service Routine (ISR) that calls ktos_WakeUp() on this specific
 * task can resume its execution and, subsequently, the scheduling of other tasks
 * (because ktos_Sleep() resets MultiTask to TRUE upon exiting).
 *
 * The system's 1ms timer tick (`ktos_timer_irq_handler`) will continue to fire
 * and update other tasks' timers and flags, but these tasks will NOT be scheduled
 * as long as the initial task remains in this ktos_Sleep(MSG_WAIT, TASK_SWITCH_INHIBIT) state.
 *
 * This combination should be used with EXTREME CAUTION and only for very specific,
 * short-duration critical sections or busy-waits where an external ISR is
 * guaranteed to provide the ktos_WakeUp() call. Improper use will lead to an
 * unresponsive system for other tasks.
 *
 * The original README warning "Do not do this lightly" for TASK_SWITCH_INHIBIT
 * particularly applies to this scenario.
 */
// KTOS.c
// Don't modify any of this file unless you really understand what you are doing
// Includes
// ========
#include "ktos_multi.h"
#include "ktos.h"
#include "ktos_hal.h"
#include <stdlib.h>
#include <stdbool.h>

// Macros and enumerations
// =======================

// Prototypes
// ==========
#ifdef __arm__
void ktos_timer_irq_handler(void) __attribute__((interrupt("IRQ")));
#else
void ktos_timer_irq_handler(void);
#endif
static void ktos_SwitchTask(void);
// Changed prototype for ktos_DefaultTaskExitHandler
static void ktos_DefaultTaskExitHandler(WORD task_return_value);

// Module variables
// ================

static struct ktos_TASK *TaskCurrent = NULL;
static bool MultiTask = TRUE;
static int32_t *OS_SP = NULL; // System Stack Pointer (used by ktos_hal_ContextSwitch)
static WORD g_LastTaskReturnValue; // Stores return value of task func across context switch

// Program
// =======

// MODIFIED ktos_DefaultTaskExitHandler
static void ktos_DefaultTaskExitHandler(WORD task_return_value)
{
#if DEBUG
  if (TaskCurrent)
  {
    ktos_DebugPrintf("Task '%c' exited with value %u.\n", TaskCurrent->TaskID, task_return_value);
  }
  else
  {
    ktos_DebugPrintf("Unknown task exited.\n");
  }
#endif
  g_LastTaskReturnValue = task_return_value;

  // This task's timeslice is over, or it explicitly exited.
  // Switch back to the OS scheduler context.
  // TaskCurrent->StackPtr will be updated by ktos_hal_ContextSwitch.
  // OS_SP is the stack pointer for ktos_SwitchTask's context.
  ktos_hal_ContextSwitch((void **)&(TaskCurrent->StackPtr), OS_SP);

  // This point should ideally not be reached if ktos_hal_ContextSwitch to OS_SP is successful
  // and the OS can schedule other tasks or idle.
  ktos_Emergency("ExitHandler_CtxSwitch_Failed");
  while(1); // Should not happen
}

struct ktos_TASK *ktos_InitTask(WORD (*Func)(WORD MsgType, WORD sParam, LONG lParam),
                      INT StackSize,
                      INT QueueSize,
                      BYTE TaskIDVal)
{
  struct ktos_TASK *Task;
  int32_t *Stack;
#if DEBUG_STATS == 1
  int t;
#endif

  Task = (struct ktos_TASK *)malloc(sizeof(struct ktos_TASK));
  if (Task == NULL) { ktos_Emergency("T Failed"); }

  Stack = (int32_t *)calloc(StackSize, sizeof(int32_t));
  if (Stack == NULL) { ktos_Emergency("S Failed"); }

  Task->MsgQueue = (struct ktos_MSG *)calloc(QueueSize, sizeof(struct ktos_MSG));
  if (Task->MsgQueue == NULL) { ktos_Emergency("Q Failed"); }

  Task->Func = Func;
  Task->TaskID = TaskIDVal;
  Task->QueueCapacity = QueueSize;

  Task->StackPtr = ktos_hal_InitTaskStack(Stack,
                                       StackSize * sizeof(int32_t),
                                       Func,
                                       ktos_DefaultTaskExitHandler, // Now expects WORD param
                                       KTOS_MSG_TYPE_INIT,
                                       (WORD)0,
                                       (LONG)0L);
  if (Task->StackPtr == NULL) { ktos_Emergency("StackInit Failed"); }

  Task->MsgQueueIn = Task->MsgQueue;
  Task->MsgQueueOut = Task->MsgQueue;
  Task->MsgQueueEnd = Task->MsgQueue + QueueSize;
  Task->Timer = 0;
  Task->TimerFlag = FALSE;
  Task->Sleeping = FALSE;
  Task->MsgCount = 0;

  if (TaskCurrent == NULL) { Task->TaskNext = Task; }
  else {
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
    while(1);
  }
  ktos_hal_InitSystemTimer(ktos_timer_irq_handler);
  ktos_hal_StartScheduler(TaskCurrent->StackPtr);
  ktos_Emergency("ktos_RunOS: ktos_hal_StartScheduler returned unexpectedly!");
  while(1);
}

bool ktos_SendMsg(struct ktos_TASK *Task, WORD MsgType, WORD sParam, LONG lParam)
{
  struct ktos_MSG *Msg;
  if (Task) {
    if (Task->MsgCount >= Task->QueueCapacity) { return false; }
    ktos_hal_DisableInterrupts();
    Msg = Task->MsgQueueIn;
    Msg->MsgType = MsgType;
    Msg->sParam = sParam;
    Msg->lParam = lParam;
    if (++Task->MsgQueueIn >= Task->MsgQueueEnd) { Task->MsgQueueIn = Task->MsgQueue; }
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
    if ((Task->Sleeping) && (!Task->TimerFlag)) {
      Task->TimerFlag = TRUE;
      Task->WakeUpType = WakeUpType;
    }
    ktos_hal_EnableInterrupts();
  }
}

// MODIFIED ktos_SwitchTask function (Phase 3: ktos_hal_ContextSwitch integration)
static void __attribute__((unused)) ktos_SwitchTask(void)
{
  // static struct ktos_MSG *Msg; // Msg is no longer passed to Task->Func by ktos_SwitchTask
  static WORD Delay;     // Will be set by g_LastTaskReturnValue

  // OS_SP is now a global static. ktos_SwitchTask runs on this OS_SP.
  // ktos_hal_StartScheduler would have set OS_SP to the initial system SP.

  while (TRUE)
  {
    ktos_hal_DisableInterrupts();
    if (MultiTask)
    {
      TaskCurrent = TaskCurrent->TaskNext;
    }

    if (TaskCurrent->Sleeping)
    {
      if (TaskCurrent->TimerFlag) // Timer expired for sleeping task
      {
        TaskCurrent->Timer = 0;
        TaskCurrent->TimerFlag = FALSE;
        TaskCurrent->Sleeping = FALSE;
        // Task is now ready to run. ktos_SwitchTask loop will continue,
        // and this task (now not sleeping) will be dispatched below.
        // No 'return' here anymore.
      }
      // else, still sleeping, loop again with interrupts enabled at end
    }

    // Check if task is ready to run (not sleeping AND has a message OR timer flag)
    // Note: A task woken by timer (TimerFlag=TRUE, Sleeping=FALSE) will be handled here.
    if (!TaskCurrent->Sleeping && (TaskCurrent->MsgCount != 0 || TaskCurrent->TimerFlag))
    {
      // If it was a timer event that made it runnable, clear the flag.
      // Message events are handled by the task itself by reading its queue.
      // The task function needs to be aware of how it was woken.
      // For now, ktos_SwitchTask still manages MsgQueueOut and MsgCount for message events.
      if (TaskCurrent->MsgCount != 0) {
          // struct ktos_MSG *current_msg = TaskCurrent->MsgQueueOut; // Task will read this
          // ktos_hal_InitTaskStack passed KTOS_MSG_TYPE_INIT. Subsequent dispatches for messages
          // mean the task needs to check its own queue.
          // We still manage the OS view of the queue here:
          if (++TaskCurrent->MsgQueueOut >= TaskCurrent->MsgQueueEnd) {
              TaskCurrent->MsgQueueOut = TaskCurrent->MsgQueue;
          }
          --TaskCurrent->MsgCount;
      }
      // If woken by timer, TimerFlag is true. Task function can check TaskCurrent->TimerFlag.
      // Clear it after task has had a chance to see it or it's for this dispatch.
      // This is tricky: if task yields, TimerFlag might be set again by ISR.
      // Let's clear TimerFlag before dispatch if it's the reason for running.
      // The task function will have to infer if it was a timer event if MsgCount was 0.
      if (TaskCurrent->MsgCount == 0 && TaskCurrent->TimerFlag) {
          // TaskCurrent->TimerFlag = FALSE; // Task will see it true, then OS clears after processing return
      }


      // --- Switch to Task Context ---
      // OS_SP (global) will be updated by ktos_hal_ContextSwitch with current OS SP.
      // TaskCurrent->StackPtr is the SP for the task to run.
      ktos_hal_ContextSwitch((void **)&OS_SP, TaskCurrent->StackPtr);
      // --- Execution resumes here in OS context when TaskCurrent yields back ---
      // Interrupts are assumed disabled by ktos_hal_ContextSwitch on return to OS.

      Delay = g_LastTaskReturnValue; // Get the task's desired sleep time

      // Process task's return value (Delay)
      if (Delay == 0) { TaskCurrent->TimerFlag = TRUE; TaskCurrent->Timer = 0;} // Yield
      else if (Delay == MSG_WAIT) { TaskCurrent->Timer = 0; TaskCurrent->TimerFlag = FALSE; } // Wait indefinitely
      else { TaskCurrent->Timer = Delay; TaskCurrent->TimerFlag = FALSE; } // ktos_Sleep for duration

      // If task was run due to timer, clear the flag *after* processing its Delay,
      // as Delay might be 0 (yield) which sets TimerFlag again.
      // The task itself should check and consume its TimerFlag if needed.
      // For now, if it was run for a timer, the timer is now reset or task is sleeping.
      // The original logic reset TimerFlag before calling Func. If it was timer event,
      // the task knew. Now it's less direct.
      // Let's assume ktos_hal_InitTaskStack passes KTOS_MSG_TYPE_TIMER if TimerFlag was the cause.
      // No, that's not how we set it up. Task must check its flags.
      // If TimerFlag was true and MsgCount was 0, then it ran due to timer.
      // The task itself should clear its TimerFlag after processing the timer event.
      // For now, ktos_SwitchTask has already reset it before this point if it was the cause.
      // This part of the logic for how a task knows *why* it ran (message vs timer)
      // when Func is no longer directly passed MsgType needs care in task design.
      // The current model: ktos_InitTask primes with KTOS_MSG_TYPE_INIT.
      // Subsequent runs: task checks its queue, checks its TimerFlag.
    }
    // else, task is sleeping and timer hasn't fired, OR task is not sleeping but no events.

    ktos_hal_EnableInterrupts();
  }
}

INT ktos_Sleep(WORD Delay, bool TaskSwitchPermit)
{
  ktos_hal_DisableInterrupts();

  TaskCurrent->Sleeping = TRUE;
  TaskCurrent->WakeUpType = 0;
  if (Delay == 0) {
    TaskCurrent->TimerFlag = TRUE;
    TaskCurrent->Timer = 0;
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

void ktos_timer_irq_handler()
{
  struct ktos_TASK *Task;
  Task = TaskCurrent;
  if (!Task) return; // Should not happen if OS is running
  do {
    if (Task->Timer) {
      if (--Task->Timer == 0) {
        Task->TimerFlag = TRUE;
      }
    }
    Task = Task->TaskNext;
  } while (Task != TaskCurrent);
}