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
#include "kdos.h"  // Should be included for struct TASK, MSG_TYPE_INIT etc.
#include "k_hal.h" // Placeholder for HAL function declarations
#include <stdlib.h>
#include <stdbool.h>

// Macros and enumerations
// =======================

// Prototypes
// ==========
void key_timer_irq_handler(void) __attribute__((interrupt("IRQ")));
static void SwitchTask(void);
static void DefaultTaskExitHandler(void); // Declare our new exit handler

// Module variables
// ================

static struct TASK *TaskCurrent = NULL;
static bool MultiTask = TRUE;
static int32_t *OS_SP = NULL; // Initialize to NULL; K_HAL_StartScheduler will set it.
static int32_t *OS_LP;        // Seems unused, consider removing later if confirmed.

// declare the stack pointer register here
// register int32_t *SP asm ("r13");

// Program
// =======

// Default handler for tasks that return from their main function.
static void DefaultTaskExitHandler(void)
{
#if DEBUG // Use DEBUG macro from KMulti.h
  if (TaskCurrent)
  { // Should always be true if a task is exiting
    DebugPrintf("Task '%c' exited normally.\n", TaskCurrent->TaskID);
  }
  else
  {
    DebugPrintf("Unknown task exited.\n");
  }
#endif
  // In a more complete OS:
  // 1. Mark TaskCurrent as terminated (e.g., TaskCurrent->State = TASK_TERMINATED;)
  // 2. Potentially release task resources (stack, TCB, queue) if dynamically managed.
  // 3. Call scheduler to switch to another task.
  //    This would involve a K_HAL_ContextSwitch back to the OS/scheduler context.
  //    For now, without that, we'll just loop to prevent running off.
  while (1)
  {
    // Halt or idle this "terminated" context.
    // A K_HAL_SuspendTask() or similar would be ideal here.
    // Or K_HAL_ContextSwitch(&TaskCurrent->StackPtr, OS_SP); to yield to OS.
  }
}

struct TASK *InitTask(WORD (*Func)(WORD MsgType, WORD sParam, LONG lParam),
                      INT StackSize, // Number of int32_t elements for the stack
                      INT QueueSize,
                      BYTE TaskIDVal) // Renamed TaskID to TaskIDVal to avoid conflict with struct member
{
  struct TASK *Task;
  int32_t *Stack; // Base of allocated stack memory
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
#if DEBUG_MEM == 1
  // debug_printf("Task  %c %04x:%04x", TaskIDVal, FP_SEG(Task), FP_OFF(Task));
#endif

  // Allocate stack: StackSize is number of int32_t elements.
  Stack = (int32_t *)calloc(StackSize, sizeof(int32_t));
  if (Stack == NULL)
  {
#if DEBUG_MEM == 1
    DebugPrintf("%c stack alloc failed", TaskIDVal);
#endif
    Emergency("S Failed");
  }
#if DEBUG_MEM == 1
  DebugPrintf("Stack %c %04x", TaskIDVal, Stack);
#endif

  Task->MsgQueue = (struct MSG *)calloc(QueueSize, sizeof(struct MSG));
  if (Task->MsgQueue == NULL)
  {
#if DEBUG_MEM == 1
    DebugPrintf("%c queue alloc failed", TaskIDVal);
#endif
    Emergency("Q Failed");
  }
#if DEBUG_MEM == 1
  DebugPrintf("Queue %c %04x", TaskIDVal, Task->MsgQueue);
#endif

  Task->Func = Func;
  Task->TaskID = TaskIDVal;        // Store the Task ID
  Task->QueueCapacity = QueueSize; // Store the queue capacity

  // Initialize the task stack using the HAL function
  // Pass MSG_TYPE_INIT as the initial message for the task.
  Task->StackPtr = K_HAL_InitTaskStack(Stack,                       // Base of stack memory
                                       StackSize * sizeof(int32_t), // Size in bytes
                                       Func,                        // Task's main function
                                       DefaultTaskExitHandler,      // Handler if Func returns
                                       MSG_TYPE_INIT,               // Initial message type
                                       (WORD)0,                     // Initial sParam
                                       (LONG)0L);                   // Initial lParam
  if (Task->StackPtr == NULL)
  {
    // K_HAL_InitTaskStack should indicate failure if stack setup is impossible
    Emergency("StackInit Failed");
  }

  Task->MsgQueueIn = Task->MsgQueue;
  Task->MsgQueueOut = Task->MsgQueue;
  Task->MsgQueueEnd = Task->MsgQueue + QueueSize;
  Task->Timer = 0;
  Task->TimerFlag = FALSE;
  Task->Sleeping = FALSE;
  Task->MsgCount = 0; // Start with zero messages; K_HAL_InitTaskStack "delivers" the first one conceptually

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

// ... (rest of Kdos.c remains the same for now) ...

void RunOS(void)
{
  // Set up your ms timer here
  SwitchTask();
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

    PUSH_REGS
    Msg = Task->MsgQueueIn;
    Msg->MsgType = MsgType;
    Msg->sParam = sParam;
    Msg->lParam = lParam;
    if (++Task->MsgQueueIn >= Task->MsgQueueEnd)
    {
      Task->MsgQueueIn = Task->MsgQueue;
    }
    ++Task->MsgCount;
    POP_REGS
    return true;
  }
  return false;
}

void WakeUp(struct TASK *Task, INT WakeUpType)
{
  if (Task)
  {
    PUSH_REGS
    if ((Task->Sleeping) && (!Task->TimerFlag))
    {
      Task->TimerFlag = TRUE;
      Task->WakeUpType = WakeUpType;
    }
    POP_REGS
  }
}

static void SwitchTask()
{
  static struct MSG *Msg;
  static WORD Delay;

  while (TRUE)
  {
    __ARMLIB_disableIRQ();
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
        __ARMLIB_enableIRQ();
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
      OS_SP = SP;
      SP = TaskCurrent->StackPtr;

      __ARMLIB_enableIRQ();
      Delay = TaskCurrent->Func(Msg->MsgType, Msg->sParam, Msg->lParam);
      __ARMLIB_disableIRQ();
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

      __ARMLIB_enableIRQ();
      Delay = TaskCurrent->Func(MSG_TYPE_TIMER, 0, 0L);
      __ARMLIB_disableIRQ();
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
    __ARMLIB_enableIRQ();
  } // while TRUE
}

INT Sleep(WORD Delay, bool TaskSwitchPermit)
{
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

  PUSH_REGS
  TaskCurrent->StackPtr = SP;
  SP = OS_SP;

  SwitchTask();

  OS_SP = SP;
  SP = TaskCurrent->StackPtr;
  POP_REGS

  MultiTask = TRUE;

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