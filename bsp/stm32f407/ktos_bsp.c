#include <stdint.h>
#include "ktos.h"

/* SysTick and NVIC registers */
#define SYSTICK_CTRL   (*(volatile uint32_t*)0xE000E010)
#define SYSTICK_LOAD   (*(volatile uint32_t*)0xE000E014)
#define SYSTICK_VAL    (*(volatile uint32_t*)0xE000E018)
#define SYSTICK_CALIB  (*(volatile uint32_t*)0xE000E01C)
#define ICSR           (*(volatile uint32_t*)0xE000ED04)
#define SHPR3          (*(volatile uint32_t*)0xE000ED20)

#define SYSTICK_CLK_HZ 168000000UL
#define PENDSV_LOWEST  (0xFFU << 16)
#define SYSTICK_LOWEST (0xFFU << 24)

void ktos_hal_DisableInterrupts(void) {
    __asm volatile ("cpsid i" ::: "memory");
}

void ktos_hal_EnableInterrupts(void) {
    __asm volatile ("cpsie i" ::: "memory");
}

uint32_t *ktos_hal_InitTaskStack(uint32_t *stack_top, void (*task_func)(void)) {
    /* Align to 8 bytes */
    uint32_t *sp = (uint32_t *)((uint32_t)stack_top & ~0x7U);

    /* Fake exception frame: xPSR, PC, LR, R12, R3, R2, R1, R0 */
    *(--sp) = 0x01000000U;            /* xPSR: Thumb bit */
    *(--sp) = (uint32_t)task_func;    /* PC */
    *(--sp) = 0xFFFFFFFEU;            /* LR: invalid return */
    *(--sp) = 0x00000000U;            /* R12 */
    *(--sp) = 0x00000000U;            /* R3 */
    *(--sp) = 0x00000000U;            /* R2 */
    *(--sp) = 0x00000000U;            /* R1 */
    *(--sp) = 0x00000000U;            /* R0 */

    /* Software-saved registers: R11..R4 */
    *(--sp) = 0x00000000U;            /* R11 */
    *(--sp) = 0x00000000U;            /* R10 */
    *(--sp) = 0x00000000U;            /* R9 */
    *(--sp) = 0x00000000U;            /* R8 */
    *(--sp) = 0x00000000U;            /* R7 */
    *(--sp) = 0x00000000U;            /* R6 */
    *(--sp) = 0x00000000U;            /* R5 */
    *(--sp) = 0x00000000U;            /* R4 */

    return sp;
}

void ktos_hal_ContextSwitch(void) {
    /* Trigger PendSV */
    ICSR = (1U << 28);
    __asm volatile ("dsb" ::: "memory");
    __asm volatile ("isb" ::: "memory");
}

void ktos_hal_StartScheduler(void) {
    /* Set PendSV and SysTick to lowest priority */
    SHPR3 |= PENDSV_LOWEST | SYSTICK_LOWEST;

    /* Load first task SP and start in Thread/Unprivileged mode */
    extern uint32_t *ktos_current_sp;
    __asm volatile (
        "ldr r0, =ktos_current_sp   \n"
        "ldr r0, [r0]               \n"
        "ldmia r0!, {r4-r11}        \n"
        "msr psp, r0                \n"
        "mov r0, #2                 \n"
        "msr control, r0            \n"
        "isb                        \n"
        "pop {r0-r3, r12, lr}       \n"
        "pop {r0}                   \n" /* PC -> skip xPSR trick via LR */
        "bx lr                      \n"
        ::: "memory"
    );
}

void ktos_hal_InitSystemTimer(uint32_t tick_hz) {
    uint32_t reload = (SYSTICK_CLK_HZ / tick_hz) - 1U;
    SYSTICK_VAL  = 0U;
    SYSTICK_LOAD = reload;
    /* Enable SysTick: ClkSrc=processor, TickInt=1, Enable=1 */
    SYSTICK_CTRL = 0x7U;
}

/* PendSV handler: context switch */
__attribute__((naked)) void PendSV_Handler(void) {
    __asm volatile (
        "mrs r0, psp                \n"
        "stmdb r0!, {r4-r11}       \n"
        /* Save current SP */
        "ldr r1, =ktos_current_sp  \n"
        "str r0, [r1]              \n"
        /* Call scheduler to update ktos_current_sp */
        "push {lr}                 \n"
        "bl ktos_scheduler         \n"
        "pop {lr}                  \n"
        /* Restore next task */
        "ldr r1, =ktos_current_sp  \n"
        "ldr r0, [r1]              \n"
        "ldmia r0!, {r4-r11}       \n"
        "msr psp, r0               \n"
        "bx lr                     \n"
        ::: "memory"
    );
}

/* SysTick handler: call KTOS tick */
void SysTick_Handler(void) {
    extern void ktos_tick(void);
    ktos_tick();
}
