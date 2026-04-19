/**
 * @file ktos_bsp.c
 * @brief KTOS Board Support Package — STM32F103 (ARM Cortex-M3)
 *
 * Supported boards: Blue Pill, Nucleo-F103RB, Maple Mini.
 *
 * | Property  | Value                         |
 * |-----------|-------------------------------|
 * | Core      | ARM Cortex-M3                 |
 * | RAM       | 20 KB                         |
 * | Flash     | 64–128 KB                     |
 * | Clock     | 72 MHz (typical)              |
 * | Toolchain | arm-none-eabi-gcc             |
 *
 * ### Build
 * @code
 * arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb --specs=nosys.specs ...
 * @endcode
 *
 * ### SysTick ISR wiring
 * Add this to your application (not inside the BSP):
 * @code
 * void SysTick_Handler(void) { ktos_timer_irq_handler(); }
 * @endcode
 *
 * @defgroup ktos_bsp_arm KTOS BSP — ARM Cortex-M (STM32F103, STM32F030)
 * @ingroup  ktos_hal
 */

#include "../../core/ktos_hal.h"
#include <stdint.h>

/* =========================================================================
 * Interrupt control
 * ========================================================================= */

/** @brief Disable all maskable interrupts (Cortex-M @c CPSID I). */
void ktos_hal_DisableInterrupts(void)
{
    __asm volatile ("cpsid i" ::: "memory");
}

/** @brief Re-enable maskable interrupts (Cortex-M @c CPSIE I). */
void ktos_hal_EnableInterrupts(void)
{
    __asm volatile ("cpsie i" ::: "memory");
}

/* =========================================================================
 * System timer — SysTick, 1 ms tick at 72 MHz
 * ========================================================================= */

/* SysTick registers */
#define SYSTICK_BASE    0xE000E010UL
#define SYSTICK_CTRL    (*(volatile uint32_t *)(SYSTICK_BASE + 0x00))
#define SYSTICK_LOAD    (*(volatile uint32_t *)(SYSTICK_BASE + 0x04))
#define SYSTICK_VAL     (*(volatile uint32_t *)(SYSTICK_BASE + 0x08))

#define SYSTICK_CTRL_ENABLE     (1 << 0)
#define SYSTICK_CTRL_TICKINT    (1 << 1)
#define SYSTICK_CTRL_CLKSOURCE  (1 << 2)

/**
 * @brief Configure SysTick for a 1 ms interrupt at 72 MHz.
 *
 */
void ktos_hal_InitSystemTimer(void (*timer_isr_addr)(void))
{
    /* 72MHz / 1000 = 72000 ticks per 1ms */
    SYSTICK_LOAD = 72000 - 1;
    SYSTICK_VAL  = 0;
    SYSTICK_CTRL = SYSTICK_CTRL_CLKSOURCE |
                   SYSTICK_CTRL_TICKINT   |
                   SYSTICK_CTRL_ENABLE;
    (void)timer_isr_addr;
    /* SysTick_Handler in the vector table must call ktos_timer_irq_handler():
     *
     * void SysTick_Handler(void) { ktos_timer_irq_handler(); }
     */
}

/* =========================================================================
 * Task stack initialisation
 * ========================================================================= */

/**
 * @brief Build the initial Cortex-M3 stack frame for a new task.
 *
 * Cortex-M hardware automatically pushes/pops R0-R3, R12, LR, PC, xPSR on
 * exceptions.  KTOS manually saves/restores R4-R11.  The synthesised frame:
 * ```
 * (low address / top of stack)
 *   R4-R11   (zeroed)      ← software-saved, restored by ContextSwitch
 *   R0       (MsgType)     ← first task argument
 *   R1       (sParam)
 *   R2       (lParam low)
 *   R3       (lParam high)
 *   R12      (0)
 *   LR       (exit handler)
 *   PC       (task entry)
 *   xPSR     (0x01000000) ← Thumb bit set
 * (high address / bottom of stack)
 * ```
 */
void *ktos_hal_InitTaskStack(void *p_stack_base,
                              unsigned int stack_size_bytes,
                              WORD (*task_func_addr)(WORD, WORD, LONG),
                              void (*task_exit_handler_addr)(WORD),
                              WORD initial_msg_type,
                              WORD initial_sparam,
                              LONG initial_lparam)
{
    uint32_t *sp = (uint32_t *)((uint8_t *)p_stack_base + stack_size_bytes);
    sp = (uint32_t *)((uint32_t)sp & ~0x7UL); /* 8-byte alignment */

    /* Hardware exception frame */
    *--sp = 0x01000000UL;                          /* xPSR — Thumb bit set */
    *--sp = (uint32_t)task_func_addr;              /* PC */
    *--sp = (uint32_t)task_exit_handler_addr;      /* LR */
    *--sp = 0;                                     /* R12 */
    *--sp = 0;                                     /* R3 */
    *--sp = (uint32_t)((initial_lparam >> 16) & 0xFFFF); /* R2 */
    *--sp = (uint32_t)(initial_sparam);            /* R1 */
    *--sp = (uint32_t)(initial_msg_type);          /* R0 */

    /* Software-saved registers R4-R11 */
    *--sp = 0; /* R11 */
    *--sp = 0; /* R10 */
    *--sp = 0; /* R9  */
    *--sp = 0; /* R8  */
    *--sp = 0; /* R7  */
    *--sp = 0; /* R6  */
    *--sp = 0; /* R5  */
    *--sp = 0; /* R4  */

    return (void *)sp;
}

/* =========================================================================
 * Context switch — Cortex-M3 assembly
 * ========================================================================= */

/**
 * @brief Cortex-M3 cooperative context switch.
 *
 * Pushes R4-R11 onto the current stack, stores SP into
 * @c *p_current_sp_storage, loads @p next_sp into SP, pops R4-R11 of the
 * new context, and returns (BX LR) into it.
 *
 * @note Naked function — no compiler-generated prologue/epilogue.
 *
 */
__attribute__((naked)) void ktos_hal_ContextSwitch(
    void **p_current_sp_storage __attribute__((unused)),
    void  *next_sp               __attribute__((unused)))
{
    __asm volatile (
        "push   {r4-r11}            \n" /* Save callee-saved registers  */
        "str    sp, [r0]            \n" /* *p_current_sp_storage = SP   */
        "mov    sp, r1              \n" /* SP = next_sp                 */
        "pop    {r4-r11}            \n" /* Restore new context          */
        "bx     lr                  \n" /* Return into new task         */
    );
}

/* =========================================================================
 * Scheduler launch
 * ========================================================================= */

/**
 * @brief Load the first task's stack and begin execution.  Never returns.
 *
 * Sets SP to @p first_task_sp, restores R4-R11, then pops the hardware
 * exception frame (R0-R3, R12, LR, PC, xPSR) to start the task.
 *
 * @note Naked function — no compiler-generated prologue/epilogue.
 *
 */
__attribute__((naked)) void ktos_hal_StartScheduler(void *first_task_sp __attribute__((unused)))
{
    __asm volatile (
        "mov    sp, r0              \n" /* Set SP to first task stack   */
        "pop    {r4-r11}            \n" /* Restore R4-R11               */
        "pop    {r0-r3, r12, lr}    \n" /* Restore R0-R3, R12, LR      */
        "pop    {pc}                \n" /* Jump to task (pop PC + xPSR) */
    );
}
