/**
 * @file ktos_bsp.c
 * @brief KTOS Board Support Package — STM32F030 (ARM Cortex-M0)
 *
 * Supported boards: Nucleo-F030R8.
 *
 * | Property  | Value                         |
 * |-----------|-------------------------------|
 * | Core      | ARM Cortex-M0                 |
 * | RAM       | 4 KB                          |
 * | Flash     | 16–64 KB                      |
 * | Clock     | 48 MHz (typical)              |
 * | Toolchain | arm-none-eabi-gcc             |
 *
 * ### Build
 * @code
 * arm-none-eabi-gcc -mcpu=cortex-m0 -mthumb --specs=nosys.specs ...
 * @endcode
 *
 * ### Cortex-M0 assembly constraints
 * - Only low registers (R0–R7) can be used with most Thumb instructions.
 * - R8–R11 must be moved to low registers via @c MOV before push/store.
 * - @c STR with SP as source is **not permitted** — use @c MOV R2, SP first.
 *
 * ### SysTick ISR wiring
 * @code
 * void SysTick_Handler(void) { ktos_timer_irq_handler(); }
 * @endcode
 *
 */

#include "../../core/ktos_hal.h"
#include <stdint.h>

/* SysTick registers */
#define SYSTICK_BASE    0xE000E010UL
#define SYSTICK_CTRL    (*(volatile uint32_t *)(SYSTICK_BASE + 0x00))
#define SYSTICK_LOAD    (*(volatile uint32_t *)(SYSTICK_BASE + 0x04))
#define SYSTICK_VAL     (*(volatile uint32_t *)(SYSTICK_BASE + 0x08))

/** @brief Disable all maskable interrupts (CPSID I). */
void ktos_hal_DisableInterrupts(void)
{
    __asm volatile ("cpsid i" ::: "memory");
}

/** @brief Re-enable maskable interrupts (CPSIE I). */
void ktos_hal_EnableInterrupts(void)
{
    __asm volatile ("cpsie i" ::: "memory");
}

/**
 * @brief Configure SysTick for a 1 ms interrupt at 48 MHz.
 *
 */
void ktos_hal_InitSystemTimer(void (*timer_isr_addr)(void))
{
    /* 48MHz / 1000 = 48000 ticks per 1ms */
    SYSTICK_LOAD = 48000 - 1;
    SYSTICK_VAL  = 0;
    SYSTICK_CTRL = (1 << 2) | (1 << 1) | (1 << 0);
    (void)timer_isr_addr;
    /* void SysTick_Handler(void) { ktos_timer_irq_handler(); } */
}

/**
 * @brief Build the initial Cortex-M0 stack frame for a new task.
 *
 * Identical layout to the Cortex-M3 BSP.  Both cores share the same
 * exception frame format (xPSR, PC, LR, R12, R3, R2, R1, R0) and the
 * KTOS software-saved R4-R11 block.
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

/**
 * @brief Cortex-M0 cooperative context switch.
 *
 * Cortex-M0 restrictions:
 * - Only R0-R7 can be pushed/popped directly.
 * - R8-R11 must be moved to low registers first via @c MOV.
 * - @c STR SP is not allowed — use @c MOV R2, SP then @c STR R2, [R0].
 *
 */
__attribute__((naked)) void ktos_hal_ContextSwitch(
    void **p_current_sp_storage __attribute__((unused)),
    void  *next_sp               __attribute__((unused)))
{
    __asm volatile (
        /* Cortex-M0: save R4-R7 then R8-R11 via low registers */
        "push   {r4-r7}             \n"
        "mov    r4, r8              \n"
        "mov    r5, r9              \n"
        "mov    r6, r10             \n"
        "mov    r7, r11             \n"
        "push   {r4-r7}             \n"
        /* Cortex-M0 cannot STR SP directly — move SP to low reg first */
        "mov    r2, sp              \n"
        "str    r2, [r0]            \n" /* *p_current_sp_storage = SP */
        "mov    sp, r1              \n" /* SP = next_sp               */
        "pop    {r4-r7}             \n"
        "mov    r8,  r4             \n"
        "mov    r9,  r5             \n"
        "mov    r10, r6             \n"
        "mov    r11, r7             \n"
        "pop    {r4-r7}             \n"
        "bx     lr                  \n"
    );
}

/**
 * @brief Load the first task's stack and begin execution.  Never returns.
 *
 * On Cortex-M0, the hardware exception frame pop must be done manually
 * because @c POP {PC} with bit-0 clear is unpredictable.  The PC is popped
 * into R6 and jumped to via @c BX R6.
 *
 */
__attribute__((naked)) void ktos_hal_StartScheduler(void *first_task_sp __attribute__((unused)))
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
