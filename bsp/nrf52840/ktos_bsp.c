#include <stdint.h>
#include <string.h>
#include "ktos.h"

/* NRF52840 SysTick and NVIC registers */
#define SYST_CSR   (*((volatile uint32_t*)0xE000E010))
#define SYST_RVR   (*((volatile uint32_t*)0xE000E014))
#define SYST_CVR   (*((volatile uint32_t*)0xE000E018))
#define ICSR       (*((volatile uint32_t*)0xE000ED04))
#define SHPR3      (*((volatile uint32_t*)0xE000ED20))
#define CPU_FREQ   64000000UL
#define TICK_HZ    1000UL

extern volatile ktos_tcb_t *ktos_current_task;
extern volatile ktos_tcb_t *ktos_next_task;

void ktos_hal_DisableInterrupts(void) {
    __asm volatile ("cpsid i" ::: "memory");
}

void ktos_hal_EnableInterrupts(void) {
    __asm volatile ("cpsie i" ::: "memory");
}

uint32_t *ktos_hal_InitTaskStack(uint32_t *stack_top, void (*task_func)(void *), void *arg) {
    uint32_t *sp = stack_top;
    *(--sp) = 0x01000000;        /* xPSR: Thumb bit */
    *(--sp) = (uint32_t)task_func; /* PC */
    *(--sp) = 0xFFFFFFFD;        /* LR: EXC_RETURN */
    *(--sp) = 0x12121212;        /* R12 */
    *(--sp) = 0x03030303;        /* R3 */
    *(--sp) = 0x02020202;        /* R2 */
    *(--sp) = 0x01010101;        /* R1 */
    *(--sp) = (uint32_t)arg;     /* R0 */
    *(--sp) = 0x11111111;        /* R11 */
    *(--sp) = 0x10101010;        /* R10 */
    *(--sp) = 0x09090909;        /* R9 */
    *(--sp) = 0x08080808;        /* R8 */
    *(--sp) = 0x07070707;        /* R7 */
    *(--sp) = 0x06060606;        /* R6 */
    *(--sp) = 0x05050505;        /* R5 */
    *(--sp) = 0x04040404;        /* R4 */
    return sp;
}

void ktos_hal_ContextSwitch(void) {
    ICSR = (1UL << 28); /* Trigger PendSV */
    __asm volatile ("dsb" ::: "memory");
    __asm volatile ("isb" ::: "memory");
}

void ktos_hal_InitSystemTimer(void) {
    SYST_RVR = (CPU_FREQ / TICK_HZ) - 1;
    SYST_CVR = 0;
    SYST_CSR = 0x07; /* Enable, tick interrupt, use processor clock */
    SHPR3 |= (0xFF << 16); /* PendSV lowest priority */
}

void ktos_hal_StartScheduler(void) {
    ktos_hal_InitSystemTimer();
    __asm volatile (
        "ldr r0, =0xE000ED08  \n"
        "ldr r0, [r0]         \n"
        "ldr r0, [r0]         \n"
        "msr msp, r0          \n"
        "cpsie i              \n"
        "svc 0                \n"
        ::: "memory"
    );
}

__attribute__((naked)) void PendSV_Handler(void) {
    __asm volatile (
        "cpsid i                    \n"
        "mrs r0, psp                \n"
        "stmdb r0!, {r4-r11}        \n"
        "ldr r1, =ktos_current_task \n"
        "ldr r1, [r1]               \n"
        "str r0, [r1]               \n"
        "ldr r1, =ktos_next_task    \n"
        "ldr r1, [r1]               \n"
        "ldr r0, [r1]               \n"
        "ldr r2, =ktos_current_task \n"
        "str r1, [r2]               \n"
        "ldmia r0!, {r4-r11}        \n"
        "msr psp, r0                \n"
        "cpsie i                    \n"
        "bx lr                      \n"
        ::: "memory"
    );
}

void SysTick_Handler(void) {
    ktos_tick();
}

void SVC_Handler(void) {
    __asm volatile (
        "ldr r1, =ktos_current_task \n"
        "ldr r1, [r1]               \n"
        "ldr r0, [r1]               \n"
        "ldmia r0!, {r4-r11}        \n"
        "msr psp, r0                \n"
        "mov r0, #2                 \n"
        "msr control, r0            \n"
        "isb                        \n"
        "pop {r0-r3, r12, lr}       \n"
        "pop {pc}                   \n"
        ::: "memory"
    );
}
