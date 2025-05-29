/*
 * WARNING: Using Sleep() with TASK_SWITCH_INHIBIT and a Delay of MSG_WAIT:
 *
 * When TASK_SWITCH_INHIBIT is used with the Sleep() function, the KDOS scheduler
 * (`SwitchTask`) will cease its normal round-robin scheduling and will focus
 * exclusively on the calling task. It will not advance to other tasks.
 *
 * If the Delay parameter is also set to MSG_WAIT (signifying an indefinite sleep),
 * the system will effectively HALT with respect to all other task activities.
 * The scheduler will continuously check only the current task, which is waiting
 * indefinitely to be woken by an explicit call to WakeUp().
 *
 * Only an Interrupt Service Routine (ISR) that calls WakeUp() on this specific
 * task can resume its execution and, subsequently, the scheduling of other tasks
 * (because Sleep() resets MultiTask to TRUE upon exiting).
 *
 * The system's 1ms timer tick (`key_timer_irq_handler`) will continue to fire
 * and update other tasks' timers and flags, but these tasks will NOT be scheduled
 * as long as the initial task remains in this Sleep(MSG_WAIT, TASK_SWITCH_INHIBIT) state.
 *
 * This combination should be used with EXTREME CAUTION and only for very specific,
 * short-duration critical sections or busy-waits where an external ISR is
 * guaranteed to provide the WakeUp() call. Improper use will lead to an
 * unresponsive system for other tasks.
 *
 * The original README warning "Do not do this lightly" for TASK_SWITCH_INHIBIT
 * particularly applies to this scenario.
 */
// KDOS.c
// Don't modify any of this file unless you really understand what you are doing
// Includes
// ========
#include "KMulti.h"
#include "kdos.h"
#include "k_hal.h"
#include <stdlib.h>
#include <stdbool.h>

// Macros and enumerations
// =======================

// Prototypes
// ==========
void key_timer_irq_handler(void) __attribute__((interrupt("IRQ")));
static void SwitchTask(void);
static void DefaultTaskExitHandler(void);

// Module variables
// ================

static struct TASK *TaskCurrent = NULL;
static bool MultiTask = TRUE;
static int32_t *OS_SP = NULL;
static int32_t *OS_LP;
static WORD g_LastTaskReturnValue; // For K_HAL_ContextSwitch integration with SwitchTask

// Program
// =======

static void DefaultTaskExitHandler(void)
{
#if DEBUG
  if (TaskCurrent)
  {
    DebugPrintf("Task '%c' exited normally.\n", TaskCurrent->TaskID);
  }
  else
  {
    DebugPrintf("Unknown task exited.\n");
  }
#endif
  // This handler will eventually need to set g_LastTaskReturnValue (e.g., to 0 or a special code)
  // and then call K_HAL_ContextSwitch(&(TaskCurrent->StackPtr), OS_SP); to yield to OS.
  while (1)
  {
  }
}

struct TASK *InitTask(WORD (*Func)(WORD MsgType, WORD sParam, LONG lParam),
                      INT StackSize,
                      INT QueueSize,
                      BYTE TaskIDVal)
{
  struct TASK *Task;
  int32_t *Stack;
#if DEBUG_STATS == 1
  int t;
#endif

  Task = (struct TASK *)malloc(sizeof(struct TASK));
  if (Task == NULL)
  {
#if DEBUG_MEM == 1
    DebugPrintf("%c task alloc failed", TaskIDVal);
#endif
    Emergency("T Failed");
  }

  Stack = (int32_t *)calloc(StackSize, sizeof(int32_t));
  if (Stack == NULL)
  {
#if DEBUG_MEM == 1
    DebugPrintf("%c stack alloc failed", TaskIDVal);
#endif
    Emergency("S Failed");
  }

  Task->MsgQueue = (struct MSG *)calloc(QueueSize, sizeof(struct MSG));
  if (Task->MsgQueue == NULL)
  {
#if DEBUG_MEM == 1
    DebugPrintf("%c queue alloc failed", TaskIDVal);
#endif
    Emergency("Q Failed");
  }

  Task->Func = Func;
  Task->TaskID = TaskIDVal;
  Task->QueueCapacity = QueueSize;

  Task->StackPtr = K_HAL_InitTaskStack(Stack,
                                       StackSize * sizeof(int32_t),
                                       Func,
                                       DefaultTaskExitHandler,
                                       MSG_TYPE_INIT,
                                       (WORD)0,
                                       (LONG)0L);
  if (Task->StackPtr == NULL)
  {
    Emergency("StackInit Failed");
  }

  Task->MsgQueueIn = Task->MsgQueue;
  Task->MsgQueueOut = Task->MsgQueue;
  Task->MsgQueueEnd = Task->MsgQueue + QueueSize;
  Task->Timer = 0;
  Task->TimerFlag = FALSE;
  Task->Sleeping = FALSE;
  Task->MsgCount = 0;

  if (TaskCurrent == NULL)
  {
    Task->TaskNext = Task;
  }
  else
  {
    Task->TaskNext = TaskCurrent->TaskNext;
    TaskCurrent->TaskNext = Task;
  }
  TaskCurrent = Task;

  return Task;
}

void RunOS(void)
{
  if (TaskCurrent == NULL)
  {
    Emergency("RunOS: No tasks initialized prior to starting OS!");
    while (1)
      ;
  }

  K_HAL_InitSystemTimer(key_timer_irq_handler);
  K_HAL_StartScheduler(TaskCurrent->StackPtr);

  Emergency("RunOS: K_HAL_StartScheduler returned unexpectedly!");
  while (1)
    ;
}

bool SendMsg(struct TASK *Task, WORD MsgType, WORD sParam, LONG lParam)
{
  struct MSG *Msg;

  if (Task)
  {
    if (Task->MsgCount >= Task->QueueCapacity)
    {
      return false;
    }

    K_HAL_DisableInterrupts();
    Msg = Task->MsgQueueIn;
    Msg->MsgType = MsgType;
    Msg->sParam = sParam;
    Msg->lParam = lParam;
    if (++Task->MsgQueueIn >= Task->MsgQueueEnd)
    {
      Task->MsgQueueIn = Task->MsgQueue;
    }
    ++Task->MsgCount;
    K_HAL_EnableInterrupts();
    return true;
  }
  return false;
}

void WakeUp(struct TASK *Task, INT WakeUpType)
{
  if (Task)
  {
    K_HAL_DisableInterrupts();
    if ((Task->Sleeping) && (!Task->TimerFlag))
    {
      Task->TimerFlag = TRUE;
      Task->WakeUpType = WakeUpType;
    }
    K_HAL_EnableInterrupts();
  }
}

static void SwitchTask()
{
  static struct MSG *Msg;
  static WORD Delay; // This will be set by g_LastTaskReturnValue after context switch

  while (TRUE)
  {
    K_HAL_DisableInterrupts();
    if (MultiTask)
    {
      TaskCurrent = TaskCurrent->TaskNext;
    }
    if (TaskCurrent->Sleeping)
    {
      if (TaskCurrent->TimerFlag)
      {
        TaskCurrent->Timer = 0;
        TaskCurrent->TimerFlag = FALSE;
        TaskCurrent->Sleeping = FALSE;
        // This 'return' is tricky. If SwitchTask is called directly from Sleep's
        // K_HAL_ContextSwitch, this 'return' would go to the OS context that called Sleep.
        // This part will be heavily affected by K_HAL_ContextSwitch integration in SwitchTask itself.
        // For now, we assume K_HAL_ContextSwitch from Sleep lands us in the scheduler loop.
        // If a task wakes up, SwitchTask will select it and switch TO it.
        // This 'return' path from SwitchTask is likely to be removed or changed
        // when K_HAL_ContextSwitch is fully integrated here.
        K_HAL_EnableInterrupts();
        return; // This return is to the C caller of SwitchTask, which is Sleep().
                // This is only valid if Sleep() calls SwitchTask() directly while on OS_SP.
                // This will change with full K_HAL_ContextSwitch integration in SwitchTask.
      }
    }
    else if (TaskCurrent->MsgCount != 0)
    {
      TaskCurrent->Timer = 0;
      TaskCurrent->TimerFlag = FALSE;
      Msg = TaskCurrent->MsgQueueOut;
      if (++TaskCurrent->MsgQueueOut == TaskCurrent->MsgQueueEnd)
      {
        TaskCurrent->MsgQueueOut = TaskCurrent->MsgQueue;
      }
      --TaskCurrent->MsgCount;

      // PHASE 3: Context switch to execute TaskCurrent->Func with this message
      // K_HAL_EnableInterrupts(); // Interrupts enabled by K_HAL_ContextSwitch before task runs
      // K_HAL_ContextSwitch(&OS_SP, TaskCurrent->StackPtr); // Task runs. On return, OS_SP is restored.
      // Delay = g_LastTaskReturnValue; // Get result
      // K_HAL_DisableInterrupts(); // Assume K_HAL_ContextSwitch returns with IRQs disabled to OS

      // Current conceptual SP switch (to be replaced)
      OS_SP = SP;
      SP = TaskCurrent->StackPtr;
      K_HAL_EnableInterrupts();
      Delay = TaskCurrent->Func(Msg->MsgType, Msg->sParam, Msg->lParam);
      K_HAL_DisableInterrupts();
      TaskCurrent->StackPtr = SP;
      SP = OS_SP;

      if (Delay == 0)
      {
        TaskCurrent->TimerFlag = TRUE;
      }
      else if (Delay == MSG_WAIT)
      {
        TaskCurrent->Timer = 0;
      }
      else
      {
        TaskCurrent->Timer = Delay;
      }
    }
    else if (TaskCurrent->TimerFlag)
    {
      TaskCurrent->Timer = 0;
      TaskCurrent->TimerFlag = FALSE;

      // PHASE 3: Context switch to execute TaskCurrent->Func with timer event
      // K_HAL_EnableInterrupts();
      // K_HAL_ContextSwitch(&OS_SP, TaskCurrent->StackPtr); // Task runs. On return, OS_SP is restored.
      // Delay = g_LastTaskReturnValue; // Get result
      // K_HAL_DisableInterrupts();

      // Current conceptual SP switch (to be replaced)
      OS_SP = SP;
      SP = TaskCurrent->StackPtr;
#if DEBUG_STATS == 1
      TaskCurrent->TimeOuts++;
#endif
      K_HAL_EnableInterrupts();
      Delay = TaskCurrent->Func(MSG_TYPE_TIMER, 0, 0L);
      K_HAL_DisableInterrupts();
      TaskCurrent->StackPtr = SP;
      SP = OS_SP;

      if (Delay == 0)
      {
        TaskCurrent->TimerFlag = TRUE;
      }
      else if (Delay == MSG_WAIT)
      {
        TaskCurrent->Timer = 0;
      }
      else
      {
        TaskCurrent->Timer = Delay;
      }
    }
    K_HAL_EnableInterrupts();
  }
}

// MODIFIED Sleep function (Phase 2: K_HAL_ContextSwitch to OS)
INT Sleep(WORD Delay, bool TaskSwitchPermit)
{
  K_HAL_DisableInterrupts();

  TaskCurrent->Sleeping = TRUE;
  TaskCurrent->WakeUpType = 0;
  if (Delay == 0)
  {
    TaskCurrent->TimerFlag = TRUE;
    TaskCurrent->Timer = 0;
  }
  else if (Delay == MSG_WAIT)
  {
    TaskCurrent->Timer = 0;
  }
  else
  {
    TaskCurrent->Timer = Delay;
  }
  MultiTask = TaskSwitchPermit;

  // Save current task's context and switch to OS stack.
  // OS_SP is the global variable holding the system stack pointer.
  // SwitchTask() will then run on the OS_SP.
  K_HAL_ContextSwitch(&(TaskCurrent->StackPtr), OS_SP);

  // --- Execution resumes here when this task is scheduled back in by SwitchTask ---
  // K_HAL_ContextSwitch (called by SwitchTask to resume this task) will have restored
  // this task's context and stack. It's assumed K_HAL_ContextSwitch returns to the
  // C code with interrupts disabled to allow for a few C operations before explicitly enabling.

  MultiTask = TRUE; // Reset for general operation after waking up

  K_HAL_EnableInterrupts(); // Enable interrupts before returning to actual task code
  return TaskCurrent->WakeUpType;
}

void key_timer_irq_handler()
{
  struct TASK *Task;

  Task = TaskCurrent;
  while (TRUE)
  {
    if (Task->Timer)
    {
      if (--Task->Timer == 0)
      {
        Task->TimerFlag = TRUE;
      }
    }
    Task = Task->TaskNext;
    if (Task == TaskCurrent)
    {
      break;
    }
  }
}