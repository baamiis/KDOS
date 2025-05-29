#ifndef K_HAL_H_INCLUDED
#define K_HAL_H_INCLUDED

// It is assumed that basic types like WORD, LONG, INT, BYTE, bool
// are defined. This typically happens in "kdos.h" or a shared "kdos_types.h"
// or "kdos_config.h". If not, k_hal.h might need to include kdos.h,
// or such a types header directly. For KDOS, "kdos.h" defines these via "KMulti.h"
// and its own typedefs effectively. Let's assume kdos.h should be included if
// those types are needed by these function signatures directly (which they are).
#include "kdos.h" // Provides WORD, LONG, INT, BYTE, bool (via stdbool.h)

// --- Interrupt Control ---

/**
 * @brief Disables all system interrupts that could interfere with KDOS critical sections.
 * Must be implemented by the BSP. This function should be re-entrant if applicable,
 * or save/restore interrupt state if nesting is required by the OS.
 * For KDOS's current usage, a simple global disable might suffice.
 */
void K_HAL_DisableInterrupts(void);

/**
 * @brief Re-enables system interrupts previously disabled by K_HAL_DisableInterrupts.
 * Must be implemented by the BSP.
 */
void K_HAL_EnableInterrupts(void);

// --- Context Switching & Task Initialization ---

/**
 * @brief Initializes the stack for a new task.
 * Sets up the initial "saved" CPU context on the task's stack.
 * When this task is first switched to via K_HAL_ContextSwitch, it should begin
 * execution at task_func_addr with the provided initial message parameters.
 * When task_func_addr returns, execution should transfer to task_exit_handler_addr,
 * passing the WORD return value of task_func_addr as an argument.
 *
 * @param p_stack_base Pointer to the base of the allocated stack memory.
 *                     Stack typically grows towards lower addresses from (p_stack_base + stack_size_bytes).
 * @param stack_size_bytes Total size of the stack memory in bytes.
 * @param task_func_addr Pointer to the C function the task will execute.
 * @param task_exit_handler_addr Pointer to a C function that will be called if task_func_addr returns.
 *                               This handler is responsible for passing the task's return value
 *                               to the OS and yielding control back.
 * @param initial_msg_type The MsgType for the task's first execution.
 * @param initial_sparam The sParam for the task's first execution.
 * @param initial_lparam The lParam for the task's first execution.
 * @return The initial stack pointer value for this task. This value will be stored in Task->StackPtr.
 *         Returns NULL on failure (e.g., if stack size is too small for initial context).
 * Must be implemented by the BSP (likely involves some assembly).
 */
void *K_HAL_InitTaskStack(void *p_stack_base,
                          unsigned int stack_size_bytes,
                          void (*task_func_addr)(WORD, WORD, LONG),
                          void (*task_exit_handler_addr)(WORD), // Now takes WORD
                          WORD initial_msg_type,
                          WORD initial_sparam,
                          LONG initial_lparam);

/**
 * @brief Performs the context switch from the current context to a new one.
 * This function saves the complete CPU context of the currently executing code
 * onto its current stack, updates its stack pointer variable (whose address is provided),
 * then switches the CPU's stack pointer to the new task's stack pointer, restores the new
 * task's previously saved CPU context, and resumes execution of the new task.
 * This function DOES NOT RETURN to the caller in the original context in a
 * conventional way; execution continues in the new task's context.
 * Interrupts should be disabled by the caller before invoking this function.
 * The HAL implementation should ensure they are re-enabled appropriately within
 * the new context if necessary, or upon returning to C code in the new context.
 *
 * @param p_current_task_sp_storage Address of the variable holding the current context's stack pointer
 *                                  (e.g., &(TaskCurrent->StackPtr) or &g_os_sp).
 *                                  The current CPU SP will be saved into the location pointed to by this address.
 * @param next_task_sp_val The stack pointer value of the context to switch to.
 * Must be implemented by the BSP (primarily in assembly).
 */
void K_HAL_ContextSwitch(void **p_current_task_sp_storage, void *next_task_sp_val);

/**
 * @brief Starts the OS scheduler and the first task.
 * This function is called once by RunOS(). It should:
 * 1. Save the current stack pointer (e.g., from main()) into the OS's global SP variable (e.g., OS_SP).
 * 2. Initiate a context switch to the first_task_stack_ptr.
 * This function does not return. Interrupts should be disabled by the caller before this call,
 * and the HAL implementation will enable them after starting the first task if appropriate.
 *
 * @param first_task_stack_ptr The initial stack pointer of the first task to run (from K_HAL_InitTaskStack).
 *                             The global OS_SP variable should be set up by this function.
 * Must be implemented by the BSP.
 */
void K_HAL_StartScheduler(void *first_task_stack_ptr);

// --- System Timer ---

/**
 * @brief Initializes and starts a hardware timer to generate periodic interrupts.
 * The timer should call the function pointed to by timer_isr_addr at regular
 * intervals (e.g., 1ms for the KDOS tick).
 *
 * @param timer_isr_addr Pointer to the C function ISR (key_timer_irq_handler from Kdos.c).
 * Must be implemented by the BSP.
 */
void K_HAL_InitSystemTimer(void (*timer_isr_addr)(void));

/**
 * @brief Optional: A macro to wrap architecture-specific ISR declaration attributes/pragmas.
 * Example for ARM GCC: #define K_HAL_ISR_FUNCTION_ATTRIBUTE __attribute__((interrupt("IRQ")))
 * If not needed by the target compiler/architecture, this can be an empty define.
 * The C-based ISR (key_timer_irq_handler) would then be declared in kdos.c as:
 *   void K_HAL_ISR_FUNCTION_ATTRIBUTE key_timer_irq_handler(void);
 * (This means kdos.c would also need to include k_hal.h for this macro).
 */
// #define K_HAL_ISR_FUNCTION_ATTRIBUTE /* architecture-specific or empty */

#endif // K_HAL_H_INCLUDED