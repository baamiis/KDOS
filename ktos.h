 // KTOS.h

#if !defined(_KTOS)
#define _KTOS

#include <stdbool.h>
#include "ktos_multi.h"

// Macros and ennumerations
// ========================

// include your target processor here
// exemple #include <targets/AT91M55800A.h>

// We rely on these as booleans for the task goes to sleep whether to allow for other tasks execution or inhibit execution until.
#define TASK_SWITCH_PERMIT 1
#define TASK_SWITCH_INHIBIT 0

#define MSG_WAIT 0xffff

// Message identifiers

enum ktos_MSG_TYPE
{
  // System message IDs
  KTOS_MSG_TYPE_INIT,
  KTOS_MSG_TYPE_TIMER,
  KTOS_MSG_TYPE_SYSTEM_START
  // User message IDs
  // Config program uses the next 2
};

// Structures
// ==========

struct ktos_TASK
{
  unsigned short int (*Func)(unsigned short int MsgType, unsigned short int sParam, long lParam);
  int32_t *StackPtr;
  struct ktos_MSG *MsgQueue;
  struct ktos_MSG *MsgQueueIn;
  struct ktos_MSG *MsgQueueOut;
  struct ktos_MSG *MsgQueueEnd;
  int MsgCount;
  INT QueueCapacity; // Added for queue overflow detection
  BYTE TaskID;       // Added to store task identifier
  unsigned short int Timer;
  bool TimerFlag;
  bool Sleeping;
  struct ktos_TASK *TaskNext;
  int WakeUpType;
};

struct ktos_MSG
{
  unsigned short int MsgType;
  unsigned short int sParam;
  long lParam;
};

// Prototypes
// ==========
void ktos_RunOS(void);
// Changed ktos_SendMsg to return bool
bool ktos_SendMsg(struct ktos_TASK *Task, unsigned short int MsgType, unsigned short int sParam, long lParam);
int ktos_Sleep(unsigned short int Delay, bool TaskSwitchPermit);
// ktos_InitTask signature is unchanged by this particular ktos.h modification
struct ktos_TASK *ktos_InitTask(unsigned short int (*Func)(unsigned short int MsgType, unsigned short int sParam, long lParam),
                      INT StackSize,
                      INT QueueSize,
                      BYTE TaskID);
void ktos_WakeUp(struct ktos_TASK *Task, INT WakeUpType);

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

#endif // !defined(_KTOS)
