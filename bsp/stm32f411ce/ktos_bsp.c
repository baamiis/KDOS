#include <stdint.h>
#include "ktos.h"

#define SYST_CSR   (*((volatile uint32_t *)0xE000E010))
#define SYST_RVR   (*((volatile uint32_t *)0xE000E014))
#define SYST_CVR   (*((volatile uint32_t *)0xE000E018))
#define SCB_ICSR   (*((volatile uint32_t *)0xE000ED04))
#define NVIC_SHPR3 (*((volatile uint32_t *)0xE000ED20))

#define SYS_CLK_HZ  96000000UL
#define TICK_HZ     1000UL

void ktos_hal_DisableInterrupts(void) {
    __asm volatile ("cpsid i" ::: "memory");
}

void ktos_hal_EnableInterrupts(void) {
    __asm volatile ("cpsie i" ::: "memory");
}

uint32_t *ktos_hal_InitTaskStack(uint32_t *stack_top, void (*task_func)(void)) {
    stack_top--;
    *stack_top = 0x01000000;        /* xPSR: Thumb bit */
    stack_top--;
    *stack_top = (uint32_t)task_func; /* PC */
    stack_top--;
    *stack_top = 0xFFFFFFFD;        /* LR: EXC_RETURN */
    stack_top--;
    *stack_top = 0x12121212;        /* R12 */
    stack_top--;
    *stack_top = 0x03030303;        /* R3 */
    stack_top--;
    *stack_top = 0x02020202;        /* R2 */
    stack_top--;
    *stack_top = 0x01010101;        /* R1 */
    stack_top--;
    *stack_top = 0x00000000;        /* R0 */
    stack_top--;
    *stack_top = 0x11111111;        /* R11 */
    stack_top--;
    *stack_top = 0x10101010;        /* R10 */
    stack_top--;
    *stack_top = 0x09090909;        /* R9 */
    stack_top--;
    *stack_top = 0x08080808;        /* R8 */
    stack_top--;
    *stack_top = 0x07070707;        /* R7 */
    stack_top--;
    *stack_top = 0x06060606;        /* R6 */
    stack_top--;
    *stack_top = 0x05050505;        /* R5 */
    stack_top--;
    *stack_top = 0x04040404;        /* R4 */
    return stack_top;
}

__attribute__((naked)) void ktos_hal_ContextSwitch(void) {
    __asm volatile (
        "push {r4-r11}          \n"
        "ldr  r0, =ktos_current_tcb \n"
        "ldr  r1, [r0]          \n"
        "str  sp, [r1]          \n"
        "bl   ktos_SelectNextTask \n"
        "ldr  r0, =ktos_current_tcb \n"
        "ldr  r1, [r0]          \n"
        "ldr  sp, [r1]          \n"
        "pop  {r4-r11}          \n"
        "bx   lr                \n"
    );
}

void SysTick_Handler(void) {
    ktos_hal_ContextSwitch();
}

void ktos_hal_InitSystemTimer(void) {
    uint32_t reload = (SYS_CLK_HZ / TICK_HZ) - 1UL;
    NVIC_SHPR3 |= (0xFF << 24);
    SYST_CVR   = 0;
    SYST_RVR   = reload;
    SYST_CSR   = 0x07;
}

void ktos_hal_StartScheduler(void) {
    ktos_hal_InitSystemTimer();
    ktos_hal_EnableInterrupts();
    while (1) {}
}
