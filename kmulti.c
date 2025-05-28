#include "KMulti.h"
#include "kdos.h"
#include <stdarg.h>
#include <stdio.h>

static WORD TaskMainProc(WORD MsgType, WORD sParam, LONG lParam);

struct TASK *TaskMain;
extern struct TASK *TaskSerial;
extern struct TASK *TaskCheckSum;

void Emergency(const char *Msg) {
  (void)Msg;
  while (1) {
  }
}

void DebugPrintf(const char *Format, ...) {
  (void)Format;
}

void InitSys(void) {
}

int main()
{
  TaskMain = InitTask(TaskMainProc,
                      TASK_MAIN_STACK_SIZE,
                      TASK_MAIN_QUEUE_SIZE,
                      TASK_MAIN_ID);
  
  if (!SendMsg(TaskMain, MSG_TYPE_INIT, 0, 0)) {
    Emergency("MainTask_InitMsg_Failed");
  }

  RunOS();
  return 0;
}

static WORD TaskMainProc(WORD MsgType, WORD sParam, LONG lParam)
{
  (void)sParam;
  (void)lParam;

  switch(MsgType) {
    case MSG_TYPE_INIT:
      InitSys();
      break;
    case MSG_TYPE_SYSTEM_START:
      break;
  }
  return MSG_WAIT;
}