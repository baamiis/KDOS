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
// static WORD g_LastTaskReturnValue; // Will be needed for K_HAL_ContextSwitch integration later

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

// MODIFIED SwitchTask function (interrupt HAL calls)
static void SwitchTask()
{
  static struct MSG *Msg;
  static WORD Delay;

  while (TRUE)
  {
    K_HAL_DisableInterrupts(); // Replaced __ARMLIB_disableIRQ
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
        K_HAL_EnableInterrupts(); // Replaced __ARMLIB_enableIRQ
        return;
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

      // Context switch logic will replace these SP assignments (Phase 2/3)
      OS_SP = SP;
      SP = TaskCurrent->StackPtr;

      K_HAL_EnableInterrupts(); // Replaced __ARMLIB_enableIRQ (before calling task)
      Delay = TaskCurrent->Func(Msg->MsgType, Msg->sParam, Msg->lParam);
      K_HAL_DisableInterrupts(); // Replaced __ARMLIB_disableIRQ (after task returns)

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

      OS_SP = SP;
      SP = TaskCurrent->StackPtr;

#if DEBUG_STATS == 1
      TaskCurrent->TimeOuts++;
#endif

      K_HAL_EnableInterrupts(); // Replaced __ARMLIB_enableIRQ (before calling task)
      Delay = TaskCurrent->Func(MSG_TYPE_TIMER, 0, 0L);
      K_HAL_DisableInterrupts(); // Replaced __ARMLIB_disableIRQ (after task returns)

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
    K_HAL_EnableInterrupts(); // Replaced __ARMLIB_enableIRQ (end of main loop iteration)
  } // while TRUE
}

// MODIFIED Sleep function (interrupt HAL calls for state modification)
INT Sleep(WORD Delay, bool TaskSwitchPermit)
{
  K_HAL_DisableInterrupts(); // Protect TaskCurrent member modifications

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

  // The PUSH_REGS/POP_REGS and SP manipulations for context switch
  // will be replaced by K_HAL_ContextSwitch in the next phase.
  // For now, K_HAL_EnableInterrupts() is called before SwitchTask, assuming
  // SwitchTask itself will immediately call K_HAL_DisableInterrupts().
  // This interaction needs to be precise when K_HAL_ContextSwitch is integrated.
  K_HAL_EnableInterrupts(); // Allow SwitchTask to manage its own critical sections.
                            // This is a temporary measure. Ideally, the section is protected until
                            // the context switch call.

  PUSH_REGS
  TaskCurrent->StackPtr = SP;
  SP = OS_SP;

  SwitchTask(); // SwitchTask begins with K_HAL_DisableInterrupts

  // --- Code resumes here when this task is scheduled back in ---
  // The K_HAL_ContextSwitch (when implemented) will return here with IRQs disabled.
  K_HAL_DisableInterrupts(); // Ensure interrupts are disabled after context restore.

  OS_SP = SP;
  SP = TaskCurrent->StackPtr;
  POP_REGS

  MultiTask = TRUE;

  K_HAL_EnableInterrupts(); // Enable interrupts before returning to task code
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