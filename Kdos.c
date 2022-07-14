// KDOS.c
// Don't modify any of this file unless you really understand what you are doing
// Includes
// ========
#include "KMulti.h"
#include <stdlib.h>

// Macros and enumerations
// =======================

// Prototypes
// ==========
void key_timer_irq_handler(void)  __attribute__ ((interrupt ("IRQ"))); //example of defining timer IRQ for ARM7
static void SwitchTask(void);

// Module variables
// ================

static struct TASK *TaskCurrent  = NULL;
static bool         MultiTask    = TRUE;
static int32_t      firstPoint   *OS_SP;
static int32_t      *OS_LP;



//declare the stack pointer register here
//register int32_t *SP asm ("r13");

// Program
// =======

// Run in system mode, once for each task.
// The first parameter is a pointer to the task function. This parameter is not
// checked (though the compiler will complain if it is the wrong type). An incorrect
// parameter should also become very obvious very quickly.
// The second and third parameters are the stack size (in 16 bit words) and the
// queue size (max number of messages). These parameters are implicitly checked by
// the result of the calls to calloc(). If KDOS is unable to allocate the memory,
// the program does not start (and in debug mode it shows the allocation that
// failed).
// The final parameter is the task ID. This should be a single ASCII character
// and is used solely for debugging purposes. It does not require checking.
// Returns a pointer to the task structure (NOT the task function) which must be
// stored for later use such as sending messages to the task.
 
struct TASK *InitTask(WORD(*Func)(WORD MsgType, WORD sParam, LONG lParam),
                      INT StackSize,
                      INT QueueSize,
                      BYTE TaskID)
{
  struct TASK *Task;
  int32_t       *Stack;
  #if DEBUG_STATS == 1
    int          t;
  #endif

  Task = (struct TASK *)malloc(sizeof(struct TASK));
  if(Task == NULL) {
    #if DEBUG_MEM == 1
      DebugPrintf("%c task alloc failed", TaskID);
    #endif
      Emergency("T Failed");
  }
  #if DEBUG_MEM == 1
    //debug_printf("Task  %c %04x:%04x", TaskID, FP_SEG(Task), FP_OFF(Task));
  #endif

  Stack = (int32_t *)calloc(StackSize, sizeof(int32_t));
  if(Stack == NULL) {
    #if DEBUG_MEM == 1
      debug_printf("%c stack alloc failed", TaskID);
    #endif
      Emergency("S Failed");
  }
  #if DEBUG_MEM == 1
    DebugPrintf("Stack %c %04x", TaskID, Stack);
  #endif

  Task->MsgQueue = (struct MSG *)calloc(QueueSize, sizeof(struct MSG));
  if(Task->MsgQueue == NULL) {
    #if DEBUG_MEM == 1
      debug_printf("%c queue alloc failed", TaskID);
    #endif
      Emergency("Q Failed");
  }
  #if DEBUG_MEM == 1
    DebugPrintf("Queue %c %04x", TaskID, Task->MsgQueue);
  #endif

  Task->Func        = Func;

  Task->StackPtr  = Stack + StackSize;
  
  Task->MsgQueueIn  = Task->MsgQueue;
  Task->MsgQueueOut = Task->MsgQueue;
  Task->MsgQueueEnd = Task->MsgQueue + QueueSize;
  Task->Timer       = 0;
  Task->TimerFlag   = FALSE;
  Task->Sleeping    = FALSE;
  if(TaskCurrent == NULL) {
    Task->TaskNext = Task;
  }
  else {
    Task->TaskNext = TaskCurrent->TaskNext;
    TaskCurrent->TaskNext = Task;
  }
  TaskCurrent = Task;
  Task->MsgCount      = 0;

  return Task;
}

// Starts the task switcher. Call once, in system mode, after the tasks
// have been set up and the system initialised.
// Parameters:
//   None

void RunOS(void)
{
  // Set up your ms timer here
  SwitchTask();
}

// Sends a message from one task to another (or even from a task to itself, if
// required). Usually run in user mode, but called once by RunOS in system mode.
// The first parameter is a pointer to the task structure. This is checked not to be NULL.
// The second parameter is the message type. This can be any value from 0-65535 and the
// message types are enumerated in KmCDOS.h. Note that 0 is pre-defined by CDOS as
// MSG_TYPE_INIT and 1 as MSG_TYPE_TIMER; you should not change these two values.
// This parameter is not checked as any value is permitted.
// The last 2 parameters are 16 and 32 bit user parameters. They are stored in the message
// structure and passed to the receiving task. The meanings of these parameters are
// user defined and checking is therefore inappropriate.
void SendMsg(struct TASK *Task, WORD MsgType, WORD sParam, LONG lParam)
{
  struct MSG *Msg;

  if(Task) {
    PUSH_REGS
    Msg          = Task->MsgQueueIn;
  Msg->MsgType = MsgType;
  Msg->sParam  = sParam;
  Msg->lParam  = lParam;
  if(++Task->MsgQueueIn >= Task->MsgQueueEnd) {
    Task->MsgQueueIn = Task->MsgQueue;
  }
  ++Task->MsgCount;
  POP_REGS
  } // if Task
}

// Wakes up a sleeping task early. Call from user mode.
// An example of the use of WakeUp would be to implement an I/O operation with timeout -
// a successful operation would call WakeUp and a timeout would occur at the end of the
// sleep period if this did not occur.
// The first parameter is a pointer to the task structure. This is checked not to be NULL.
// The second parameter specifies the WakeUpType, i.e. the value that the Sleep() call
// will return to the sleeping task. This would be 0 if the task had timed out, so use a
// WakeUpType of non-0 if the task needs to know how it woke up.

void WakeUp(struct TASK *Task, INT WakeUpType)
{
  if(Task) {
    PUSH_REGS
    if((Task->Sleeping) && (!Task->TimerFlag)) {
      Task->TimerFlag  = TRUE;
      Task->WakeUpType = WakeUpType;
    }
    POP_REGS
  }
}

// Recursive. Entered in system mode, partly executed in user mode. Called only by
// other KDOS routines and not directly by the application rogram.

static void SwitchTask()
{
  // Msg and Delay are static because they are referenced in both system and user modes.
  // However, being static means their value is not retained after a recursive call,
  // so they will be invalid after control has passed to a user task and returned.
  static struct MSG *Msg;
  static WORD        Delay;

  while(TRUE) {
    __ARMLIB_disableIRQ();
    if(MultiTask) {
      TaskCurrent = TaskCurrent->TaskNext;
    }
    if(TaskCurrent->Sleeping) { // Check timer flag
      if(TaskCurrent->TimerFlag) {
        TaskCurrent->Timer     = 0;
        TaskCurrent->TimerFlag = FALSE;
        TaskCurrent->Sleeping  = FALSE;
         __ARMLIB_enableIRQ();
        // Return to Sleep() still in system mode
        return;
      }
    }
    else if(TaskCurrent->MsgCount != 0) { // Not sleeping, check Q
      TaskCurrent->Timer     = 0;
      TaskCurrent->TimerFlag = FALSE;
      Msg = TaskCurrent->MsgQueueOut;
      if(++TaskCurrent->MsgQueueOut == TaskCurrent->MsgQueueEnd) {
        TaskCurrent->MsgQueueOut = TaskCurrent->MsgQueue;
      }
      --TaskCurrent->MsgCount;
      OS_SP = SP;                 // Save system stack
      SP = TaskCurrent->StackPtr; // Load user stack

      // ---------------- User mode -----------------

      __ARMLIB_enableIRQ();
      Delay = TaskCurrent->Func(Msg->MsgType, Msg->sParam, Msg->lParam);
      __ARMLIB_disableIRQ();
      TaskCurrent->StackPtr = SP; // Save user stack
      SP = OS_SP;                         // Load system stack
 
      // ---------------- System mode ---------------

      if(Delay == 0) {
        TaskCurrent->TimerFlag = TRUE;
      }
      else if(Delay == MSG_WAIT) {
        TaskCurrent->Timer = 0;
      }
      else {
        TaskCurrent->Timer = Delay;
      }
    }
    else if(TaskCurrent->TimerFlag) { // Not sleeping, no msgs, check timer
      TaskCurrent->Timer     = 0;
      TaskCurrent->TimerFlag = FALSE;
      OS_SP = SP;                         // Save system stack
      SP = TaskCurrent->StackPtr; // Load user stack

      // ---------------- User mode -----------------

      #if DEBUG_STATS == 1
        TaskCurrent->TimeOuts++;
      #endif

      __ARMLIB_enableIRQ();
      Delay = TaskCurrent->Func(MSG_TYPE_TIMER, 0, 0L);
      __ARMLIB_disableIRQ();
      TaskCurrent->StackPtr = SP; // Save user stack
      SP = OS_SP; // Load system stack

      // ---------------- System mode ---------------

      if(Delay == 0) {
        TaskCurrent->TimerFlag = TRUE;
      }
      else if(Delay == MSG_WAIT) {
        TaskCurrent->Timer = 0;
      }
      else {
        TaskCurrent->Timer = Delay;
      }
    }
    __ARMLIB_enableIRQ();
  } // while TRUE
}
// Sleeps for the specified number of milliseconds.
// Recursive. Enters in user mode, partly executed in system mode.
// It is permissible to enter this routine with interrupts already disabled (though they
// always will be enabled upon exit). This is to allow TaskCurrent->Sleeping to be set
// after an I/O function has been initiated but before an interrupt can call WakeUp() to
// check the status of this flag.
// The first parameter specifies the number of milliseconds to sleep, from 0 to 65534.
// The sleep time will expire between n and (n - 1) ms, though control will not
// necessarily be returned immediately if another task has control at that time.
// There are 2 special cases. A sleep time of 0 will cause no specific delay but gives
// the opportunity to all other tasks to execute if they are ready. This should be used
// periodically if a task has a job to do that will take a long time (always assuming
// that the task is divisible). A sleep time of 65535 (MSG_WAIT) means sleep forever.
// This is not used very often, but is not as silly as it might sound - a task that is
// sleeping forever may be woken by another task calling WakeUp.
// The second parameter specifies if CDOS may despatch other tasks during the sleep. In
// nearly every case this should be set to TASK_SWITCH_PERMIT, but if it is necessary
// to implement a delay without the sleeping task surrendering control, it may be set
// to TASK_SWITCH_INHIBIT. Do not do this lightly.
// The function returns 0 if the specified time expires. If it returns as a result of
// a WakeUp call then the return value is that specified by the wake up.

INT Sleep(WORD Delay, bool TaskSwitchPermit)
{
  //__ARMLIB_disableIRQ();
  //disable interrupts here

  TaskCurrent->Sleeping         = TRUE;
  TaskCurrent->WakeUpType       = 0;
  if(Delay == 0) {
    TaskCurrent->TimerFlag      = TRUE;
    TaskCurrent->Timer          = 0;
  }
  else if(Delay == MSG_WAIT) {
    TaskCurrent->Timer          = 0;
  }
  else {
    TaskCurrent->Timer          = Delay;
  }
  MultiTask                     = TaskSwitchPermit;

  PUSH_REGS
  TaskCurrent->StackPtr = SP; // Save user stack
  SP = OS_SP;                 // Load system stack

  // --------------- System mode ---------------

  //__ARMLIB_enableIRQ();
  //enable interrupts here
  SwitchTask();
  //disable interrupts here
  //__ARMLIB_disableIRQ();

  OS_SP = SP;                 // Save system stack
  SP = TaskCurrent->StackPtr; // Load user stack
  POP_REGS
 
  // ---------------- User mode -----------------

  MultiTask = TRUE;

  //__ARMLIB_enableIRQ();
  //enable interrupt here

  return TaskCurrent->WakeUpType;
}

// Timer IRQ Handler. 1mS Timer tick
//   None

void key_timer_irq_handler()
{
  struct TASK *Task;

  Task = TaskCurrent;
  while(TRUE) {
    if(Task->Timer) {
      if(--Task->Timer == 0) {
        Task->TimerFlag = TRUE;
      }
    }
    Task = Task->TaskNext;
    if(Task == TaskCurrent) {
      break;
    }
  }
}