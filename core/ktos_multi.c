#include "ktos_multi.h"
#include "ktos.h"
#include <stdarg.h>
#include <stdio.h>

static WORD ktos_TaskMainProc(WORD MsgType, WORD sParam, LONG lParam);

struct ktos_TASK *TaskMain;
extern struct ktos_TASK *TaskSerial;
extern struct ktos_TASK *TaskCheckSum;

void ktos_Emergency(const char *Msg) {
  (void)Msg;
  while (1) {
  }
}

void ktos_DebugPrintf(const char *Format, ...) {
  (void)Format;
}

void ktos_InitSys(void) {
}

int main()
{
  TaskMain = ktos_InitTask(ktos_TaskMainProc,
                      TASK_MAIN_STACK_SIZE,
                      TASK_MAIN_QUEUE_SIZE,
                      TASK_MAIN_ID);
  
  if (!ktos_SendMsg(TaskMain, KTOS_MSG_TYPE_INIT, 0, 0)) {
    ktos_Emergency("MainTask_InitMsg_Failed");
  }

  ktos_RunOS();
  return 0;
}

static WORD ktos_TaskMainProc(WORD MsgType, WORD sParam, LONG lParam)
{
  (void)sParam;
  (void)lParam;

  switch(MsgType) {
    case KTOS_MSG_TYPE_INIT:
      ktos_InitSys();
      break;
    case KTOS_MSG_TYPE_SYSTEM_START:
      break;
  }
  return MSG_WAIT;
}