// KMulti.c

// This is the main module for the Kentec Syncro multi-loop fire control panel.
// It contains the main() function and main task procedure and is responsible for
// calling all the initialisers in the other modules.

// There is a risk that these requirements influence the program design away from a
// modular approach and towards a more monolithic structure, which in general would
// not be desirable. The approach we have taken is more practical and achievable - the
// code is designed such that:
// a) we don't supply incorrect parameters in the first place,
// b) wherever possible an incorrect parameter should not cause the program to crash,
// c) if it does crash it should not do so in a way that prevents the watchdog timer
//    from operating,
// d) we provide a debug facility for use during development and test, where incorrect
//    parameters may be reported.

// Includes
// ========
#include "KMulti.h"

#include <stdarg.h>
#include <stdio.h>

// Macros
// ======

// Local prototypes
// ================

static WORD TaskMainProc(WORD MsgType, WORD sParam, LONG lParam);
static void EmergencyInt(const char *Msg);
static void EmergencyWritePrinter(const char *Format, ...);

// Global variables
// ================

struct TASK *TaskMain;
extern struct TASK *TaskSerial;
extern struct TASK *TaskCheckSum;

// Module variables
// ================

// Program
// =======

int main()
{

  // Initialise ALL tasks, send a message to at least one of them and then call RunOS().
  TaskMain = InitTask(TaskMainProc,
                      TASK_MAIN_STACK_SIZE,
                      TASK_MAIN_QUEUE_SIZE,
                      TASK_MAIN_ID);
  SendMsg(TaskMain, MSG_TYPE_INIT, 0, 0);  // Must be first message

  //Initialise all other tasks here.

  RunOS(); //run the KDOS scheduler
}

// Main Task function initialised in main(). Returns message wait
// Processes System start message
// Parameters:
//   MsgType: 16 bit value defined by the call that sends the message. 
//            Message types are enumnerated in KDOS.h, with 0
//   sParam: User defined 16 bit value
//   lParam: User defined 32 bit value
static WORD TaskMainProc(WORD MsgType, WORD sParam, LONG lParam)
{
  switch(MsgType) {

    case MSG_TYPE_INIT:
      // Don't print anything here, serial port not initialised
      // But I wonder which bit isn't?
      InitSys();
      break;

    case MSG_TYPE_SYSTEM_START:
      // System integrity check can hapened here
      break;

  }

  return MSG_WAIT;
}