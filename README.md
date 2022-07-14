 
 # What is KDOS?
 
 KDOS is a simple co-operative task switcher. It allows different parts of the
 program to operate largely independently. It is very tiny in size arround 4K
 
 # When KDOS can be used?
 
 if you have not enough memory to handle other existing realtime operating systems like FreeRTOS for example 
 KDOS is good tiny alternative. It can run on any processor as long as you correctly set the stack pointer.   
  
 
 # How does it work?

 Each task has its own stack and message queue, the sizes of which can be
 controlled independently. The system stack size and queues sizes must be reasonably estimated.

 Tasks communicate by means of simple messages. Messages arrive in the sequence
 in which they are sent. Each task has an associated timer with a resolution of
 1ms and maximum time of 65 seconds.
 
 Tasks are executed on a simple "round robin" basis, provided the task has a
 message in its queue or its timer has expired. Once a task gets control it
 will not be pre-empted. This has the advantage that semaphores and similar
 mechanisms are not required when accessing shared global variables, but it
 does place a responsibility on the programmer to not retain control for
 excessively long periods. KDOS provides the facility to surrender control to
 other tasks yet regain control immediately once those other tasks have had their
 opportunity to execute.

 Each task has a single "task function" which is called whenever a message is
 available. There are two basic methods of using the task function - it may
 terminate after handling each message, or it may remain in a forever loop
 with a Sleep() call surrendering control to other tasks. Note, however, that
 while this second method works fine for things like a 1 second timer task,
 it cannot be used where the task expects to receive other messages, as it
 will not receive a new message until the task procedure terminates. Note too
 that the task function is in many ways just an ordinary C function - if you
 remain in the function using only Sleep() to surrender control, any local
 variables will remain valid. If you terminate the function and receive a new
 message, the function is being called again and local variables will not
 have retained their values (unless they are static).

 The task function supplies 3 parameters.
 The first is the MsgType, which is a 16 bit value defined by the call that
 sends the message. Message types are enumnerated in KDOS.h, with 0
 (MSG_TYPE_INIT) and 1 (MSG_TYPE_TIMER) pre-defined.
 The second and third parameters are user defined 16 bit and 32 bit values
 respectively. CDOS assigns no meaning to these these parameters, this must
 be mutually agreed between the task that sends the message and the task
 receiving the message.

 "System mode" refers to when KDOS is using its own stack, "User mode" refers
 to when KDOS has loaded certain task stack.
 
 Also note when accessing global variables that are used in interrupt handlers
 (though strictly this isn't anything to do with KDOS) you should first disable
 interrupts (preferably first saving the existing state of the interrupt flag).