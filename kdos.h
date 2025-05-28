// KDOS.h

#if !defined(_KDOS)
#define _KDOS

// Macros and ennumerations
// ========================

// include your target processor here
// exemple #include <targets/AT91M55800A.h>

// We rely on these as booleans for the task goes to sleep whether to allow for
other tasks execution or inhibit execution until.
#define TASK_SWITCH_PERMIT 1
#define TASK_SWITCH_INHIBIT 0

#define MSG_WAIT 0xffff

                         // Message identifiers

                         enum MSG_TYPE {
                           // System message IDs
                           MSG_TYPE_INIT,
                           MSG_TYPE_TIMER
                           // User message IDs
                           // Config program uses the next 2
                         };

// Structures
// ==========

struct TASK
{
  unsigned short int (*Func)(unsigned short int MsgType, unsigned short int sPara m, long lParam);
  int32_t *StackPtr; // Corrected line
  struct MSG *MsgQueue;
  struct MSG *MsgQueueIn;
  struct MSG *MsgQueueOut;
  struct MSG *MsgQueueEnd;
  int MsgCount;
  unsigned short int Timer;
  bool TimerFlag;
  bool Sleeping;
  struct TASK *TaskNext;
  int WakeUpType;
};

struct MSG
{
  unsigned short int MsgType;
  unsigned short int sParam;
  long lParam;
};

// Prototypes
// ==========
void RunOS(void);
void SendMsg(struct TASK *Task, unsigned short int MsgType, unsigned short int s Param, long lParam);
int Sleep(unsigned short int Delay, bool TaskSwitchPermit);
struct TASK *InitTask(unsigned short int (*Func)(unsigned short int MsgType, unsi gned short int sParam, long lParam),
                      INT StackSize,
                      INT QueueSize,
                      BYTE TaskID);
void WakeUp(struct TASK *Task, INT WakeUpType);

// Stack sizes
// ===========

// Declare sizes for the stacks for each of your tasks. They must be big enough
// for the task itself and all nested subroutines, allowing for parameters and
// local variables as well as the calls themselves. Allow for recursion if your
// program uses it. Allow for interrupts as in general these do not load their
// own stack.
#define TASK_MAIN_STACK_SIZE 512

// Queue sizes
// ===========

// Declare sizes for the queues. If a queue overflows you will lose messages
// without warning.
#define TASK_MAIN_QUEUE_SIZE 3
// define the stack sizes for your tasks here

// Task IDs
// ========

#define TASK_MAIN_ID 'M'
// define your task IDs here

#endif // !defined(_KDOS)