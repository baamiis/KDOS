/*
 * KTOS BSP for STM32F030 (Nucleo-F030R8)
 * Core: ARM Cortex-M0
 * RAM:  4KB
 * Flash: 16-64KB
 * Clock: 48MHz (typical)
 *
 * Toolchain: arm-none-eabi-gcc
 * Compile:   arm-none-eabi-gcc -mcpu=cortex-m0 -mthumb ...
 *
 * NOTE: Cortex-M0 does not support STMDB/LDMIA with SP directly.
 * Push/pop must be done carefully — only low registers (R0-R7) usable
 * in most thumb instructions. R8-R11 require MOV to low registers first.
 */

#include "../../core/ktos_hal.h"
#include <stdint.h>

/* SysTick registers */
#define SYSTICK_BASE    0xE000E010UL
#define SYSTICK_CTRL    (*(volatile uint32_t *)(SYSTICK_BASE + 0x00))
#define SYSTICK_LOAD    (*(volatile uint32_t *)(SYSTICK_BASE + 0x04))
#define SYSTICK_VAL     (*(volatile uint32_t *)(SYSTICK_BASE + 0x08))

void ktos_hal_DisableInterrupts(void)
{
    __asm volatile ("cpsid i" ::: "memory");
}

void ktos_hal_EnableInterrupts(void)
{
    __asm volatile ("cpsie i" ::: "memory");
}

void ktos_hal_InitSystemTimer(void (*timer_isr_addr)(void))
{
    /* 48MHz / 1000 = 48000 ticks per 1ms */
    SYSTICK_LOAD = 48000 - 1;
    SYSTICK_VAL  = 0;
    SYSTICK_CTRL = (1 << 2) | (1 << 1) | (1 << 0);
    (void)timer_isr_addr;
    /* void SysTick_Handler(void) { ktos_timer_irq_handler(); } */
}

void *ktos_hal_InitTaskStack(void *p_stack_base,
                              unsigned int stack_size_bytes,
                              void (*task_func_addr)(WORD, WORD, LONG),
                              void (*task_exit_handler_addr)(WORD),
                              WORD initial_msg_type,
                              WORD initial_sparam,
                              LONG initial_lparam)
{
    uint32_t *sp = (uint32_t *)((uint8_t *)p_stack_base + stack_size_bytes);
    sp = (uint32_t *)((uint32_t)sp & ~0x7UL);

    *--sp = 0x01000000UL;
    *--sp = (uint32_t)task_func_addr;
    *--sp = (uint32_t)task_exit_handler_addr;
    *--sp = 0;
    *--sp = 0;
    *--sp = (uint32_t)((initial_lparam >> 16) & 0xFFFF);
    *--sp = (uint32_t)(initial_sparam);
    *--sp = (uint32_t)(initial_msg_type);

    /* R4-R11 */
    for (int i = 0; i < 8; i++) *--sp = 0;

    return (void *)sp;
}

__attribute__((naked)) void ktos_hal_ContextSwitch(void **p_current_sp_storage,
                                                    void *next_sp)
{
    __asm volatile (
        /* Cortex-M0: save R4-R7 then R8-R11 via mov */
        "push   {r4-r7}             \n"
        "mov    r4, r8              \n"
        "mov    r5, r9              \n"
        "mov    r6, r10             \n"
        "mov    r7, r11             \n"
        "push   {r4-r7}             \n"
        "str    sp, [r0]            \n" /* save current SP */
        "mov    sp, r1              \n" /* load next SP    */
        "pop    {r4-r7}             \n"
        "mov    r8,  r4             \n"
        "mov    r9,  r5             \n"
        "mov    r10, r6             \n"
        "mov    r11, r7             \n"
        "pop    {r4-r7}             \n"
        "bx     lr                  \n"
    );
}

__attribute__((naked)) void ktos_hal_StartScheduler(void *first_task_sp)
{
    __asm volatile (
        "mov    sp, r0              \n"
        "pop    {r4-r7}             \n"
        "mov    r8,  r4             \n"
        "mov    r9,  r5             \n"
        "mov    r10, r6             \n"
        "mov    r11, r7             \n"
        "pop    {r4-r7}             \n"
        "pop    {r0-r3}             \n"
        "pop    {r4}                \n" /* R12 */
        "pop    {r5}                \n" /* LR  */
        "pop    {r6}                \n" /* PC  */
        "pop    {r7}                \n" /* xPSR - discard */
        "bx     r6                  \n"
    );
}
