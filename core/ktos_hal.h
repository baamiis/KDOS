/**
 * @file ktos_hal.h
 * @brief KTOS Hardware Abstraction Layer (HAL) interface.
 *
 * BSP authors implement every function declared here for their specific
 * microcontroller.  The KTOS kernel calls these functions but has no
 * knowledge of the underlying hardware.
 *
 * ### Porting checklist
 * Implement all six functions in @c bsp/\<mcu\>/ktos_bsp.c:
 *
 * | Function                    | Language   | Notes                                 |
 * |-----------------------------|------------|---------------------------------------|
 * | ktos_hal_DisableInterrupts  | C or ASM   | Global interrupt disable              |
 * | ktos_hal_EnableInterrupts   | C or ASM   | Global interrupt enable               |
 * | ktos_hal_InitTaskStack      | C (+ ASM)  | Stack frame setup                     |
 * | ktos_hal_ContextSwitch      | ASM        | Save/restore CPU registers            |
 * | ktos_hal_StartScheduler     | ASM        | First-task launch, never returns      |
 * | ktos_hal_InitSystemTimer    | C          | 1 ms periodic hardware timer          |
 *
 * @defgroup ktos_hal KTOS HAL — hardware abstraction layer
 */

#ifndef ktos_hal_H_INCLUDED
#define ktos_hal_H_INCLUDED

#include "ktos.h" /* provides WORD, LONG, INT, BYTE via ktos_multi.h */

/* =========================================================================
 * Interrupt control
 * ========================================================================= */

/**
 * @ingroup ktos_hal
 * @brief Disable all maskable interrupts.
 *
 * Called by the KTOS kernel to protect critical sections (queue updates,
 * task-state changes).  Must not be called from within an ISR.
 *
 * ### AVR example
 * @code
 * void ktos_hal_DisableInterrupts(void) { cli(); }
 * @endcode
 *
 * ### Cortex-M example
 * @code
 * void ktos_hal_DisableInterrupts(void) { __disable_irq(); }
 * @endcode
 */
void ktos_hal_DisableInterrupts(void);

/**
 * @ingroup ktos_hal
 * @brief Re-enable maskable interrupts.
 *
 * Paired with ktos_hal_DisableInterrupts().  Every disable must be
 * followed by exactly one enable.
 *
 * ### AVR example
 * @code
 * void ktos_hal_EnableInterrupts(void) { sei(); }
 * @endcode
 *
 * ### Cortex-M example
 * @code
 * void ktos_hal_EnableInterrupts(void) { __enable_irq(); }
 * @endcode
 */
void ktos_hal_EnableInterrupts(void);

/* =========================================================================
 * Context switching and task initialisation
 * ========================================================================= */

/**
 * @ingroup ktos_hal
 * @brief Set up the initial stack frame for a new task.
 *
 * This function crafts a fake "saved context" at the top of the task's
 * stack so that when ktos_hal_ContextSwitch() first switches to this task
 * it begins execution at @p task_func_addr with the three initial parameters.
 *
 * When @p task_func_addr returns, execution must transfer to
 * @p task_exit_handler_addr with the task's return value (a @c WORD) as the
 * sole argument.  The exit handler performs the final context switch back to
 * the OS scheduler.
 *
 * @param p_stack_base            Pointer to the start (lowest address) of the
 *                                allocated stack buffer.
 * @param stack_size_bytes        Size of that buffer in bytes.
 * @param task_func_addr          Entry point of the task function.
 * @param task_exit_handler_addr  Called when the task function returns; receives
 *                                the task's @c WORD return value.
 * @param initial_msg_type        @c MsgType passed to the task on first dispatch
 *                                (always @c KTOS_MSG_TYPE_INIT on startup).
 * @param initial_sparam          @c sParam for the first dispatch.
 * @param initial_lparam          @c lParam for the first dispatch.
 * @return  The initial stack pointer value to store in @c Task->StackPtr.
 *          Returns @c NULL if the stack is too small for the frame.
 *
 * @note The stack layout is architecture-specific.  Study an existing BSP
 *       (e.g. @c bsp/stm32f103/ktos_bsp.c) before writing a new one.
 */
void *ktos_hal_InitTaskStack(void          *p_stack_base,
                              unsigned int   stack_size_bytes,
                              WORD         (*task_func_addr)(WORD, WORD, LONG),
                              void         (*task_exit_handler_addr)(WORD),
                              WORD           initial_msg_type,
                              WORD           initial_sparam,
                              LONG           initial_lparam);

/**
 * @ingroup ktos_hal
 * @brief Perform a cooperative context switch between two execution contexts.
 *
 * Saves the full CPU register set of the **current** context onto its stack,
 * records the new stack-pointer value into @p p_current_sp_storage,
 * loads @p next_sp into the CPU's stack pointer, then restores
 * the register set of the **next** context and resumes it.
 *
 * From the C caller's perspective this function appears to return normally;
 * execution actually continues in the next context and only "returns" here
 * when the scheduler switches back to this context later.
 *
 * @param p_current_sp_storage  Address of the variable that holds this
 *                                   context's saved stack pointer
 *                                   (e.g. @c &TaskCurrent->StackPtr or
 *                                   @c &OS_SP).  Updated on entry.
 * @param next_sp           Stack pointer value of the context to
 *                                   switch into (from a previous call to
 *                                   ktos_hal_InitTaskStack() or a previous
 *                                   ktos_hal_ContextSwitch()).
 *
 * @note Must be implemented in assembly.  Interrupts must be disabled by
 *       the caller before invoking this function.
 *
 * ### Cortex-M3 skeleton
 * @code
 * __attribute__((naked)) void ktos_hal_ContextSwitch(
 *         void **p_current_sp_storage __attribute__((unused)),
 *         void  *next_sp          __attribute__((unused)))
 * {
 *     __asm volatile(
 *         "push   {r4-r11}          \n"
 *         "str    sp, [r0]          \n"  // save current SP → *p_current_sp_storage
 *         "mov    sp, r1            \n"  // load next SP
 *         "pop    {r4-r11}          \n"
 *         "bx     lr                \n"
 *     );
 * }
 * @endcode
 *
 * @note On **Cortex-M0** you cannot use @c STR with SP as source.
 *       Route SP through a low register first:
 * @code
 *         "mov    r2, sp            \n"
 *         "str    r2, [r0]          \n"
 * @endcode
 */
void ktos_hal_ContextSwitch(void **p_current_sp_storage,
                             void  *next_sp);

/**
 * @ingroup ktos_hal
 * @brief Launch the KTOS scheduler and the first task.  Never returns.
 *
 * Called once by ktos_RunOS() after all tasks have been initialised.
 * The implementation must:
 * 1. Store the current OS stack pointer into the kernel's @c OS_SP variable
 *    so the scheduler can return to it later via ktos_hal_ContextSwitch().
 * 2. Load @p first_task_sp into the CPU stack pointer and restore the
 *    task's initial register context (as laid out by ktos_hal_InitTaskStack()).
 * 3. Enable interrupts and begin executing the first task.
 *
 * @param first_task_sp  Initial stack pointer of the first task to run
 *                              (the value returned by ktos_hal_InitTaskStack()).
 *
 * @note Must be implemented in assembly.  This function must not return.
 */
void ktos_hal_StartScheduler(void *first_task_sp);

/* =========================================================================
 * System timer
 * ========================================================================= */

/**
 * @ingroup ktos_hal
 * @brief Configure and start the 1 ms system tick timer.
 *
 * Sets up a hardware timer (SysTick, Timer1, FRC1, …) to fire an interrupt
 * exactly every 1 ms and call @p timer_isr_addr inside that ISR.
 *
 * The KTOS scheduler relies on this 1 ms resolution to track task timers and
 * sleep durations accurately.
 *
 * @param timer_isr_addr  Pointer to @c ktos_timer_irq_handler() from ktos.c.
 *                        The BSP must call this function from within its timer ISR.
 *
 * ### AVR Timer1 CTC example (16 MHz, prescaler 64, OCR1A = 249)
 * @code
 * void ktos_hal_InitSystemTimer(void (*isr)(void))
 * {
 *     g_ktos_timer_isr = isr;
 *     TCCR1A = 0;
 *     TCCR1B = (1 << WGM12) | (1 << CS11) | (1 << CS10); // CTC, prescaler 64
 *     OCR1A  = 249;                                         // 1 ms at 16 MHz
 *     TIMSK1 = (1 << OCIE1A);
 * }
 * // ISR calls g_ktos_timer_isr() directly.
 * @endcode
 *
 * ### Cortex-M SysTick example (72 MHz)
 * @code
 * void ktos_hal_InitSystemTimer(void (*isr)(void))
 * {
 *     g_ktos_timer_isr = isr;
 *     SysTick_Config(72000); // 72 MHz / 72000 = 1 kHz
 * }
 * void SysTick_Handler(void) { g_ktos_timer_isr(); }
 * @endcode
 */
void ktos_hal_InitSystemTimer(void (*timer_isr_addr)(void));

#endif /* ktos_hal_H_INCLUDED */
