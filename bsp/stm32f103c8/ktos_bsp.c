#include <stdint.h>
#include "ktos.h"

#define SYST_CSR   (*((volatile uint32_t *)0xE000E010))
#define SYST_RVR   (*((volatile uint32_t *)0xE000E014))
#define SYST_CVR   (*((volatile uint32_t *)0xE000E018))
#define ICSR       (*((volatile uint32_t *)0xE000ED04))
#define SHPR3      (*((volatile uint32_t *)0xE000ED20))
#define SYSTICK_HZ 1000
#define SYS_CLOCK  72000000UL

void ktos_hal_DisableInterrupts(void) {
    __asm volatile ("CPSID I");
}

void ktos_hal_EnableInterrupts(void) {
    __asm volatile ("CPSIE I");
}

uint32_t *ktos_hal_InitTaskStack(uint32_t *stack_top, void (*task_func)(void)) {
    stack_top--;
    *stack_top-- = 0x01000000;          /* xPSR: thumb bit */
    *stack_top-- = (uint32_t)task_func; /* PC */
    *stack_top-- = 0xFFFFFFFD;          /* LR: EXC_RETURN */
    *stack_top-- = 0x12121212;          /* R12 */
    *stack_top-- = 0x03030303;          /* R3 */
    *stack_top-- = 0x02020202;          /* R2 */
    *stack_top-- = 0x01010101;          /* R1 */
    *stack_top-- = 0x00000000;          /* R0 */
    *stack_top-- = 0x11111111;          /* R11 */
    *stack_top-- = 0x10101010;          /* R10 */
    *stack_top-- = 0x09090909;          /* R9 */
    *stack_top-- = 0x08080808;          /* R8 */
    *stack_top-- = 0x07070707;          /* R7 */
    *stack_top-- = 0x06060606;          /* R6 */
    *stack_top-- = 0x05050505;          /* R5 */
    *stack_top   = 0x04040404;          /* R4 */
    return stack_top;
}

__attribute__((naked)) void ktos_hal_ContextSwitch(void) {
    __asm volatile (
        "PUSH {R4-R11}\n"
        "LDR  R0, =ktos_current_task\n"
        "LDR  R1, [R0]\n"
        "STR  SP, [R1]\n"
        "LDR  R0, =ktos_next_task\n"
        "LDR  R1, [R0]\n"
        "LDR  R0, =ktos_current_task\n"
        "STR  R1, [R0]\n"
        "LDR  SP, [R1]\n"
        "POP  {R4-R11}\n"
        "BX   LR\n"
    );
}

void PendSV_Handler(void) __attribute__((alias("ktos_hal_ContextSwitch")));

void SysTick_Handler(void) {
    ktos_tick();
    ICSR = (1u << 28);
}

void ktos_hal_InitSystemTimer(void) {
    SHPR3 |= (0xFF << 16);
    SYST_RVR = (SYS_CLOCK / SYSTICK_HZ) - 1;
    SYST_CVR = 0;
    SYST_CSR = 0x07;
}

__attribute__((naked)) void ktos_hal_StartScheduler(void) {
    __asm volatile (
        "LDR  R0, =ktos_current_task\n"
        "LDR  R1, [R0]\n"
        "LDR  SP, [R1]\n"
        "POP  {R4-R11}\n"
        "POP  {R0-R3, R12, LR}\n"
        "ADD  SP, SP, #4\n"
        "POP  {PC}\n"
    );
}
