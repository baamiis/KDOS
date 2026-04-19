/*
 * KTOS BSP for STM32F103 (Blue Pill, Nucleo-F103RB)
 * Core: ARM Cortex-M3
 * RAM:  20KB
 * Flash: 64-128KB
 * Clock: 72MHz (typical)
 *
 * Toolchain: arm-none-eabi-gcc
 * Compile:   arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb ...
 */

#include "../../core/ktos_hal.h"
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Interrupt Control                                                    */
/* ------------------------------------------------------------------ */

void ktos_hal_DisableInterrupts(void)
{
    __asm volatile ("cpsid i" ::: "memory");
}

void ktos_hal_EnableInterrupts(void)
{
    __asm volatile ("cpsie i" ::: "memory");
}

/* ------------------------------------------------------------------ */
/* System Timer — SysTick, 1ms tick at 72MHz                           */
/* ------------------------------------------------------------------ */

/* SysTick registers */
#define SYSTICK_BASE    0xE000E010UL
#define SYSTICK_CTRL    (*(volatile uint32_t *)(SYSTICK_BASE + 0x00))
#define SYSTICK_LOAD    (*(volatile uint32_t *)(SYSTICK_BASE + 0x04))
#define SYSTICK_VAL     (*(volatile uint32_t *)(SYSTICK_BASE + 0x08))

#define SYSTICK_CTRL_ENABLE     (1 << 0)
#define SYSTICK_CTRL_TICKINT    (1 << 1)
#define SYSTICK_CTRL_CLKSOURCE  (1 << 2)

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

/* ------------------------------------------------------------------ */
/* Task Stack Initialisation                                            */
/* ------------------------------------------------------------------ */
/*
 * Cortex-M3 exception stack frame (auto-saved by hardware on exception):
 *   xPSR, PC, LR, R12, R3, R2, R1, R0   ← pushed by CPU
 * Software-saved context:
 *   R4-R11                               ← pushed by context switch code
 *
 * Full initial stack layout (top = lowest address, stack grows down):
 *   R4, R5, R6, R7, R8, R9, R10, R11    ← software saved
 *   R0 (initial_msg_type)
 *   R1 (initial_sparam)
 *   R2 (initial_lparam low)
 *   R3 (initial_lparam high)
 *   R12
 *   LR  (task_exit_handler_addr)
 *   PC  (task_func_addr)
 *   xPSR (0x01000000 = Thumb mode)
 */

void *ktos_hal_InitTaskStack(void *p_stack_base,
                              unsigned int stack_size_bytes,
                              void (*task_func_addr)(WORD, WORD, LONG),
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

/* ------------------------------------------------------------------ */
/* Context Switch — Cortex-M3 assembly (naked function)                */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/* Start Scheduler                                                      */
/* ------------------------------------------------------------------ */

__attribute__((naked)) void ktos_hal_StartScheduler(void *first_task_sp __attribute__((unused)))
{
    __asm volatile (
        "mov    sp, r0              \n" /* Set SP to first task stack   */
        "pop    {r4-r11}            \n" /* Restore R4-R11               */
        "pop    {r0-r3, r12, lr}    \n" /* Restore R0-R3, R12, LR      */
        "pop    {pc}                \n" /* Jump to task (pop PC + xPSR) */
    );
}
