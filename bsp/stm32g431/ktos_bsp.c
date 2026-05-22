#include <stdint.h>
#include "../../core/ktos_hal.h"

#define SYST_CSR  (*(volatile uint32_t*)0xE000E010)
#define SYST_RVR  (*(volatile uint32_t*)0xE000E014)
#define SYST_CVR  (*(volatile uint32_t*)0xE000E018)
#define ICSR      (*(volatile uint32_t*)0xE000ED04)
#define SHPR3     (*(volatile uint32_t*)0xE000ED20)
#define NVIC_ISER (*(volatile uint32_t*)0xE000E100)

#define SYS_CLOCK_HZ 170000000UL

static void (*systick_isr_cb)(void);

void ktos_hal_DisableInterrupts(void) {
    __asm volatile ("cpsid i");
}

void ktos_hal_EnableInterrupts(void) {
    __asm volatile ("cpsie i");
}

void *ktos_hal_InitTaskStack(void *stack_top, void (*task_func)(void)) {
    uint32_t *sp = (uint32_t *)stack_top;
    *(--sp) = 0x01000000;          /* xPSR: Thumb bit */
    *(--sp) = (uint32_t)task_func; /* PC */
    *(--sp) = 0;                   /* LR */
    *(--sp) = 0;                   /* R12 */
    *(--sp) = 0;                   /* R3 */
    *(--sp) = 0;                   /* R2 */
    *(--sp) = 0;                   /* R1 */
    *(--sp) = 0;                   /* R0 */
    /* software-saved: R11-R4 */
    *(--sp) = 0; *(--sp) = 0; *(--sp) = 0; *(--sp) = 0;
    *(--sp) = 0; *(--sp) = 0; *(--sp) = 0; *(--sp) = 0;
    return sp;
}

void ktos_hal_ContextSwitch(void **current_sp, void *next_sp) {
    __asm volatile (
        "push {r4-r11}          \n"
        "str  sp, [r0]          \n"
        "mov  sp, r1            \n"
        "pop  {r4-r11}          \n"
        "bx   lr                \n"
    );
    (void)current_sp; (void)next_sp;
}

void ktos_hal_StartScheduler(void *first_task_sp) {
    __asm volatile (
        "mov  sp, r0            \n"
        "pop  {r4-r11}          \n"
        "pop  {r0-r3, r12, lr}  \n"
        "pop  {pc}              \n"
    );
    (void)first_task_sp;
}

void SysTick_Handler(void) {
    if (systick_isr_cb) systick_isr_cb();
}

void ktos_hal_InitSystemTimer(void (*isr)(void)) {
    systick_isr_cb = isr;
    SYST_RVR = (SYS_CLOCK_HZ / 1000) - 1;
    SYST_CVR = 0;
    SYST_CSR = 0x07; /* CLKSOURCE | TICKINT | ENABLE */
}
