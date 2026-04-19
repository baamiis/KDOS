/**
 * @file ktos.h
 * @brief KTOS public API — include this in your application code.
 *
 * KTOS is a tiny cooperative task switcher (~4 KB footprint) for
 * resource-constrained embedded systems.  Tasks communicate via messages,
 * run on a round-robin basis, and never pre-empt each other — so shared
 * global variables need no semaphores.
 *
 * ### Minimal usage example
 * @code
 * #include "ktos.h"
 *
 * static struct ktos_TASK *g_led_task;
 *
 * WORD led_task(WORD MsgType, WORD sParam, LONG lParam)
 * {
 *     switch (MsgType) {
 *         case KTOS_MSG_TYPE_INIT:
 *             // initialise GPIO here
 *             break;
 *         case KTOS_MSG_TYPE_TIMER:
 *             // toggle LED here
 *             break;
 *     }
 *     return 500; // sleep 500 ms, then wake with KTOS_MSG_TYPE_TIMER
 * }
 *
 * int main(void)
 * {
 *     g_led_task = ktos_InitTask(led_task, TASK_MAIN_STACK_SIZE,
 *                                TASK_MAIN_QUEUE_SIZE, TASK_MAIN_ID);
 *     ktos_RunOS(); // never returns
 * }
 * @endcode
 *
 * @defgroup ktos_core  KTOS Core — scheduler and task management
 * @defgroup ktos_types KTOS Types — structs, enums, and constants
 */

#if !defined(_KTOS)
#define _KTOS

#include <stdbool.h>
#include "ktos_multi.h"

/* =========================================================================
 * Constants
 * ========================================================================= */

/**
 * @ingroup ktos_core
 * @brief Pass to ktos_Sleep() to allow the scheduler to run other tasks
 *        while this task is sleeping.
 *
 * This is the normal mode — the task sleeps and the scheduler advances
 * to the next ready task.
 */
#define TASK_SWITCH_PERMIT  1

/**
 * @ingroup ktos_core
 * @brief Pass to ktos_Sleep() to prevent the scheduler from running any
 *        other task while this task is sleeping.
 *
 * @warning Combining with @c MSG_WAIT as the delay causes the entire system
 *          to halt until an ISR calls ktos_WakeUp() on this task.  Use
 *          with extreme caution and only for very short, critical sections.
 */
#define TASK_SWITCH_INHIBIT 0

/**
 * @ingroup ktos_core
 * @brief Return this value from a task function to sleep indefinitely until
 *        explicitly woken by ktos_WakeUp() or an incoming message.
 */
#define MSG_WAIT 0xffff

/* =========================================================================
 * Enumerations
 * ========================================================================= */

/**
 * @ingroup ktos_types
 * @brief Built-in message type identifiers delivered by the KTOS scheduler.
 *
 * Your task function receives one of these as @c MsgType on every invocation.
 * Values above @c KTOS_MSG_TYPE_SYSTEM_START are free for application use.
 *
 * @code
 * WORD my_task(WORD MsgType, WORD sParam, LONG lParam)
 * {
 *     switch (MsgType) {
 *         case KTOS_MSG_TYPE_INIT:  // first call — hardware init
 *             break;
 *         case KTOS_MSG_TYPE_TIMER: // periodic tick
 *             break;
 *         default:                  // user-defined message
 *             break;
 *     }
 *     return 100; // sleep 100 ms
 * }
 * @endcode
 */
enum ktos_MSG_TYPE
{
    KTOS_MSG_TYPE_INIT,         /**< Delivered once when the task first runs. */
    KTOS_MSG_TYPE_TIMER,        /**< Delivered when the task's sleep timer expires. */
    KTOS_MSG_TYPE_SYSTEM_START  /**< Reserved for internal OS use. */
    /* User message IDs start here */
};

/* =========================================================================
 * Structures
 * ========================================================================= */

/**
 * @ingroup ktos_types
 * @brief Represents a single message in a task's queue.
 *
 * Messages are queued via ktos_SendMsg() and consumed by the scheduler
 * before each task invocation.
 */
struct ktos_MSG
{
    unsigned short int MsgType; /**< Message type — built-in or user-defined. */
    unsigned short int sParam;  /**< 16-bit user-defined parameter. */
    long               lParam;  /**< 32-bit user-defined parameter. */
};

/**
 * @ingroup ktos_types
 * @brief KTOS task control block (TCB).
 *
 * One instance per task.  Created and initialised by ktos_InitTask() —
 * do not fill this structure manually.
 *
 * @note All fields are managed by the KTOS kernel.  Application code should
 *       treat this as an opaque handle and only pass pointers to the API
 *       functions (ktos_SendMsg(), ktos_WakeUp(), etc.).
 */
struct ktos_TASK
{
    unsigned short int (*Func)(unsigned short int MsgType,
                               unsigned short int sParam,
                               long               lParam); /**< Task entry function. */
    int32_t          *StackPtr;      /**< Current stack pointer (updated on each context switch). */
    struct ktos_MSG  *MsgQueue;      /**< Base of the circular message queue. */
    struct ktos_MSG  *MsgQueueIn;    /**< Write pointer into the message queue. */
    struct ktos_MSG  *MsgQueueOut;   /**< Read pointer out of the message queue. */
    struct ktos_MSG  *MsgQueueEnd;   /**< One-past-end sentinel for wrap-around. */
    int               MsgCount;      /**< Number of messages currently queued. */
    INT               QueueCapacity; /**< Maximum number of messages the queue can hold. */
    BYTE              TaskID;        /**< User-assigned single-byte task identifier (e.g. 'M'). */
    unsigned short int Timer;        /**< Countdown timer in milliseconds (decremented by ISR). */
    bool              TimerFlag;     /**< Set by the ISR when @c Timer reaches zero. */
    bool              Sleeping;      /**< True when the task is waiting for a timer or message. */
    struct ktos_TASK *TaskNext;      /**< Next task in the circular scheduling ring. */
    int               WakeUpType;    /**< Value passed by the most recent ktos_WakeUp() call. */
};

/* =========================================================================
 * API
 * ========================================================================= */

/**
 * @ingroup ktos_core
 * @brief Create and register a new task with the KTOS scheduler.
 *
 * Allocates a task control block and stack, initialises the task's message
 * queue, and inserts the task into the circular scheduling ring.
 * Call this for every task before calling ktos_RunOS().
 *
 * @param Func       Pointer to the task function.  Signature:
 *                   @code WORD task(WORD MsgType, WORD sParam, LONG lParam); @endcode
 *                   Return value is the sleep duration in milliseconds, or
 *                   @c MSG_WAIT to sleep until explicitly woken.
 * @param StackSize  Stack depth in 32-bit words (not bytes).  Must be large
 *                   enough for the deepest call chain, all local variables,
 *                   and interrupt frame overhead.  Start with
 *                   @c TASK_MAIN_STACK_SIZE (512 words) and reduce if RAM
 *                   is tight.
 * @param QueueSize  Maximum number of messages the task can queue at once.
 *                   Messages sent to a full queue are silently dropped.
 *                   @c TASK_MAIN_QUEUE_SIZE (3) is a safe default.
 * @param TaskID     Single-byte identifier for debug output (e.g. @c 'M' for
 *                   main task, @c 'S' for sensor task).
 * @return           Pointer to the newly created task control block.
 *                   Pass this to ktos_SendMsg() and ktos_WakeUp().
 *
 * @note ktos_Emergency() is called and execution halts if any allocation fails.
 *
 * ### Example
 * @code
 * struct ktos_TASK *sensor = ktos_InitTask(sensor_task, 256, 4, 'S');
 * struct ktos_TASK *led    = ktos_InitTask(led_task,    128, 2, 'L');
 * ktos_RunOS();
 * @endcode
 */
struct ktos_TASK *ktos_InitTask(
    unsigned short int (*Func)(unsigned short int MsgType,
                               unsigned short int sParam,
                               long               lParam),
    INT  StackSize,
    INT  QueueSize,
    BYTE TaskID);

/**
 * @ingroup ktos_core
 * @brief Start the KTOS scheduler.  Never returns.
 *
 * Initialises the 1 ms system tick timer and transfers control to the first
 * registered task.  All tasks must be created with ktos_InitTask() before
 * calling this function.
 *
 * @note If called with no tasks registered, ktos_Emergency() is invoked.
 *
 * @code
 * ktos_InitTask(my_task, TASK_MAIN_STACK_SIZE, TASK_MAIN_QUEUE_SIZE, 'M');
 * ktos_RunOS(); // execution never reaches here
 * @endcode
 */
void ktos_RunOS(void);

/**
 * @ingroup ktos_core
 * @brief Enqueue a message for delivery to a task.
 *
 * Thread-safe: interrupts are briefly disabled while writing to the queue.
 * May be called from ISR context.
 *
 * If the target task's queue is full the message is discarded and @c false
 * is returned — no assertion or error is raised.
 *
 * @param Task     Target task (returned by ktos_InitTask()).
 * @param MsgType  Message type identifier (built-in or user-defined WORD).
 * @param sParam   16-bit user-defined parameter.
 * @param lParam   32-bit user-defined parameter.
 * @return         @c true  — message queued successfully.
 * @return         @c false — queue full or @p Task is NULL; message dropped.
 *
 * ### Example — notify a task from an ISR
 * @code
 * void USART_RX_IRQHandler(void)
 * {
 *     uint8_t byte = USART->DR;
 *     ktos_SendMsg(g_uart_task, MSG_UART_RX, byte, 0);
 * }
 * @endcode
 */
bool ktos_SendMsg(struct ktos_TASK   *Task,
                  unsigned short int  MsgType,
                  unsigned short int  sParam,
                  long                lParam);

/**
 * @ingroup ktos_core
 * @brief Yield the CPU for a given duration and optionally allow other tasks to run.
 *
 * The task suspends itself for @p Delay milliseconds.  When the timer
 * expires the scheduler delivers @c KTOS_MSG_TYPE_TIMER on the next
 * invocation.  Call from task context only — never from an ISR.
 *
 * Equivalent to returning @p Delay directly from the task function; the
 * difference is that ktos_Sleep() can be invoked deep inside a call stack.
 *
 * @param Delay             Sleep duration in milliseconds.
 *                          - @c 0       — yield immediately (run other tasks once, then resume).
 *                          - @c 1–65534 — sleep for that many milliseconds.
 *                          - @c MSG_WAIT — sleep indefinitely until ktos_WakeUp() is called.
 * @param TaskSwitchPermit  @c TASK_SWITCH_PERMIT (1) — allow other tasks to run while sleeping.
 *                          @c TASK_SWITCH_INHIBIT (0) — freeze scheduler; only this task
 *                          can resume (requires ISR to call ktos_WakeUp()).
 * @return                  The @c WakeUpType value supplied by the ktos_WakeUp() call that
 *                          woke this task, or @c 0 if woken by the timer.
 *
 * @warning Passing both @c MSG_WAIT and @c TASK_SWITCH_INHIBIT halts all
 *          other tasks until an ISR calls ktos_WakeUp().  Use with extreme caution.
 *
 * ### Example
 * @code
 * // Inside a task function — wait 200 ms then continue
 * ktos_Sleep(200, TASK_SWITCH_PERMIT);
 * // Execution resumes here after 200 ms
 * @endcode
 */
int ktos_Sleep(unsigned short int Delay, bool TaskSwitchPermit);

/**
 * @ingroup ktos_core
 * @brief Wake a sleeping task immediately from an ISR or another task.
 *
 * Sets the task's @c TimerFlag so the scheduler will dispatch it on the
 * next scheduling cycle.  Safe to call from ISR context.
 *
 * Has no effect if the task is not currently sleeping.
 *
 * @param Task       Task to wake (returned by ktos_InitTask()).
 * @param WakeUpType Arbitrary value returned to the sleeping task by
 *                   ktos_Sleep().  Use this to communicate *why* the task
 *                   was woken (e.g. which peripheral triggered the event).
 *
 * ### Example — wake a task from a button ISR
 * @code
 * void EXTI0_IRQHandler(void)
 * {
 *     ktos_WakeUp(g_ui_task, WAKE_BUTTON_PRESS);
 * }
 *
 * // Inside g_ui_task:
 * int reason = ktos_Sleep(MSG_WAIT, TASK_SWITCH_PERMIT);
 * if (reason == WAKE_BUTTON_PRESS) { handle_button(); }
 * @endcode
 */
void ktos_WakeUp(struct ktos_TASK *Task, INT WakeUpType);

/* =========================================================================
 * Default stack / queue sizes and task IDs
 * ========================================================================= */

/**
 * @ingroup ktos_types
 * @brief Default stack depth (in 32-bit words) for the main task.
 *
 * Increase this value if the task uses deep call chains or large local arrays.
 * Each word is 4 bytes, so 512 words = 2 KB of RAM.
 */
#define TASK_MAIN_STACK_SIZE 512

/**
 * @ingroup ktos_types
 * @brief Default message queue depth for the main task.
 *
 * If more than this many messages are sent before the task processes them,
 * excess messages are silently dropped.
 */
#define TASK_MAIN_QUEUE_SIZE 3

/**
 * @ingroup ktos_types
 * @brief Single-byte task identifier used in debug output for the main task.
 */
#define TASK_MAIN_ID 'M'

#endif /* !defined(_KTOS) */
