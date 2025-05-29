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
#include "KMulti.h" // Should ideally include kdos.h or kdos.c should include kdos.h directly
#include <stdlib.h>
#include <stdbool.h> // For bool type

// Macros and enumerations
// =======================

// Prototypes
// ==========
void key_timer_irq_handler(void) __attribute__((interrupt("IRQ"))); // example o
f defining timer IRQ for ARM7
static void SwitchTask(void);

// Module variables
// ================

static struct TASK *TaskCurrent = NULL;
static bool MultiTask = TRUE;
static int32_t *OS_SP;
static int32_t *OS_LP;

// declare the stack pointer register here
// register int32_t *SP asm ("r13");

// Program
// =======

struct TASK *InitTask(WORD (*Func)(WORD MsgType, WORD sParam, LONG lParam),
                      INT StackSize,
                      INT QueueSize,
                      BYTE TaskID)
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
    DebugPrintf("%c task alloc failed", TaskID);
#endif
    Emergency("T Failed");
  }
#if DEBUG_MEM == 1
  // debug_printf("Task  %c %04x:%04x", TaskID, FP_SEG(Task), FP_OFF(Task));
#endif

  Stack = (int32_t *)calloc(StackSize, sizeof(int32_t));
  if (Stack == NULL)
  {
#if DEBUG_MEM == 1
    debug_printf("%c stack alloc failed", TaskID);
#endif
    Emergency("S Failed");
  }
#if DEBUG_MEM == 1
  DebugPrintf("Stack %c %04x", TaskID, Stack);
#endif

  Task->MsgQueue = (struct MSG *)calloc(QueueSize, sizeof(struct MSG));
  if (Task->MsgQueue == NULL)
  {
#if DEBUG_MEM == 1
    debug_printf("%c queue alloc failed", TaskID);
#endif
    Emergency("Q Failed");
  }
#if DEBUG_MEM == 1
  DebugPrintf("Queue %c %04x", TaskID, Task->MsgQueue);
#endif

  Task->Func = Func;
  Task->StackPtr = Stack + StackSize; // Assumes StackPtr is int32_t* now
  Task->QueueCapacity = QueueSize;    // Store the queue capacity
  Task->MsgQueueIn = Task->MsgQueue;
  Task->MsgQueueOut = Task->MsgQueue;
  Task->MsgQueueEnd = Task->MsgQueue + QueueSize;
  Task->Timer = 0;
  Task->TimerFlag = FALSE;
  Task->Sleeping = FALSE;
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
  Task->MsgCount = 0;

  return Task;
}

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
    // Check if the queue is full
    if (Task->MsgCount >= Task->QueueCapacity)
    {
      // Optionally, call Emergency() or DebugPrintf() here for queue full
      return false; // Queue is full
    }

    PUSH_REGS // Placeholder for interrupt disable / critical section
        Msg = Task->MsgQueueIn;
    Msg->MsgType = MsgType;
    Msg->sParam = sParam;
    Msg->lParam = lParam;
    if (++Task->MsgQueueIn >= Task->MsgQueueEnd)
    {
      Task->MsgQueueIn = Task->MsgQueue;
    }
    ++Task->MsgCount;
    POP_REGS         // Placeholder for interrupt enable / end critical section
        return true; // Message sent successfully
  } // if Task
  return false; // Task was NULL
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