#include <stdint.h>
#include "../../core/ktos_hal.h"

#define SYST_CSR   (*((volatile uint32_t*)0xE000E010))
#define SYST_RVR   (*((volatile uint32_t*)0xE000E014))
#define SYST_CVR   (*((volatile uint32_t*)0xE000E018))
#define SCB_ICSR   (*((volatile uint32_t*)0xE000ED04))
#define PRIMASK_IE  1u

#define CPU_HZ     64000000UL
#define TICK_HZ    1000UL

static void (*s_tick_isr)(void);

void ktos_hal_DisableInterrupts(void) {
    __asm volatile ("cpsid i" ::: "memory");
}

void ktos_hal_EnableInterrupts(void) {
    __asm volatile ("cpsie i" ::: "memory");
}

void *ktos_hal_InitTaskStack(void *stack_top, void (*task_func)(void)) {
    uint32_t *sp = (uint32_t *)stack_top;
    *--sp = 0x01000000u;          /* xPSR  - Thumb bit */
    *--sp = (uint32_t)task_func;  /* PC    */
    *--sp = 0u;                   /* LR    */
    *--sp = 0u;                   /* R12   */
    *--sp = 0u;                   /* R3    */
    *--sp = 0u;                   /* R2    */
    *--sp = 0u;                   /* R1    */
    *--sp = 0u;                   /* R0    */
    /* software-saved: R11-R4 */
    *--sp = 0u; *--sp = 0u; *--sp = 0u; *--sp = 0u;
    *--sp = 0u; *--sp = 0u; *--sp = 0u; *--sp = 0u;
    return sp;
}

__attribute__((naked)) void ktos_hal_ContextSwitch(void **current_sp, void *next_sp) {
    __asm volatile (
        "push {r4-r11}          \n"
        "str  sp, [r0]          \n"
        "mov  sp, r1            \n"
        "pop  {r4-r11}          \n"
        "bx   lr                \n"
    );
}

__attribute__((naked)) void ktos_hal_StartScheduler(void *first_task_sp) {
    __asm volatile (
        "mov  sp, r0            \n"
        "pop  {r4-r11}          \n"
        "pop  {r0-r3,r12,lr}    \n"
        "pop  {r0}              \n"
        "pop  {r1}              \n"
        "bx   r1                \n"
    );
}

void SysTick_Handler(void) {
    if (s_tick_isr) s_tick_isr();
}

void ktos_hal_InitSystemTimer(void (*isr)(void)) {
    s_tick_isr = isr;
    SYST_RVR = (CPU_HZ / TICK_HZ) - 1u;
    SYST_CVR = 0u;
    SYST_CSR = 0x7u;  /* CLKSOURCE=1, TICKINT=1, ENABLE=1 */
}
