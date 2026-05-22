#include <stdint.h>
#include "../../core/ktos_hal.h"

#define SYST_CSR  (*(volatile uint32_t*)0xE000E010)
#define SYST_RVR  (*(volatile uint32_t*)0xE000E014)
#define SYST_CVR  (*(volatile uint32_t*)0xE000E018)
#define ICSR      (*(volatile uint32_t*)0xE000ED04)
#define SHPR3     (*(volatile uint32_t*)0xE000ED20)

#define HSI_FREQ  16000000UL
#define SYSTICK_1MS (HSI_FREQ / 1000 - 1)

static void (*systick_isr_cb)(void);

void ktos_hal_DisableInterrupts(void) {
    __asm volatile ("cpsid i" ::: "memory");
}

void ktos_hal_EnableInterrupts(void) {
    __asm volatile ("cpsie i" ::: "memory");
}

void *ktos_hal_InitTaskStack(void *stack_top, void (*task_func)(void)) {
    uint32_t *sp = (uint32_t *)stack_top;
    *--sp = 0x01000000;          /* xPSR: Thumb bit */
    *--sp = (uint32_t)task_func; /* PC */
    *--sp = 0xFFFFFFFD;          /* LR (EXC_RETURN) */
    *--sp = 0x12121212;          /* R12 */
    *--sp = 0x03030303;          /* R3 */
    *--sp = 0x02020202;          /* R2 */
    *--sp = 0x01010101;          /* R1 */
    *--sp = 0x00000000;          /* R0 */
    *--sp = 0x11111111;          /* R11 */
    *--sp = 0x10101010;          /* R10 */
    *--sp = 0x09090909;          /* R9 */
    *--sp = 0x08080808;          /* R8 */
    *--sp = 0x07070707;          /* R7 */
    *--sp = 0x06060606;          /* R6 */
    *--sp = 0x05050505;          /* R5 */
    *--sp = 0x04040404;          /* R4 */
    return (void *)sp;
}

void ktos_hal_ContextSwitch(void **current_sp, void *next_sp) {
    __asm volatile (
        "push {r4-r11}          \n"
        "str  sp, [%0]          \n"
        "mov  sp, %1            \n"
        "pop  {r4-r11}          \n"
        : : "r"(current_sp), "r"(next_sp) : "memory"
    );
}

void ktos_hal_StartScheduler(void *first_task_sp) {
    __asm volatile (
        "mov  sp, %0            \n"
        "pop  {r4-r11}          \n"
        "pop  {r0-r3,r12,lr}    \n"
        "pop  {r0}              \n"
        "pop  {r1}              \n"
        "bx   lr                \n"
        : : "r"(first_task_sp) : "memory"
    );
}

void SysTick_Handler(void) {
    if (systick_isr_cb) systick_isr_cb();
}

void ktos_hal_InitSystemTimer(void (*isr)(void)) {
    systick_isr_cb = isr;
    SHPR3 |= (0xFFU << 24);
    SYST_RVR = SYSTICK_1MS;
    SYST_CVR = 0;
    SYST_CSR = 0x07;
}
