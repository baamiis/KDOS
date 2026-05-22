/*
 * KTOS BSP for STM32G431 (Cortex-M4F, 170 MHz)
 * Implements the 6 HAL functions declared in ktos_hal.h.
 */
#include <stdint.h>
#include "ktos.h"
#include "ktos_hal.h"

#define SYST_CSR   (*((volatile uint32_t *)0xE000E010))
#define SYST_RVR   (*((volatile uint32_t *)0xE000E014))
#define SYST_CVR   (*((volatile uint32_t *)0xE000E018))
#define NVIC_SHPR3 (*((volatile uint32_t *)0xE000ED20))

#define SYS_CLK_HZ 170000000UL
#define TICK_HZ    1000UL

static void (*g_ktos_timer_isr)(void);

extern void *OS_SP;

void ktos_hal_DisableInterrupts(void) {
    __asm volatile ("cpsid i" ::: "memory");
}

void ktos_hal_EnableInterrupts(void) {
    __asm volatile ("cpsie i" ::: "memory");
}

void *ktos_hal_InitTaskStack(void          *p_stack_base,
                              unsigned int   stack_size_bytes,
                              WORD         (*task_func_addr)(WORD, WORD, LONG),
                              void         (*task_exit_handler_addr)(WORD),
                              WORD           initial_msg_type,
                              WORD           initial_sparam,
                              LONG           initial_lparam)
{
    if (stack_size_bytes < 72u) return (void *)0;

    uint32_t *sp = (uint32_t *)((uint8_t *)p_stack_base + stack_size_bytes);
    /* Align to 8 bytes */
    sp = (uint32_t *)((uint32_t)sp & ~7u);

    /* --- Hardware-saved frame (auto pushed on exception entry) --- */
    /* xPSR */
    sp--; *sp = 0x01000000u;
    /* PC = task entry */
    sp--; *sp = ((uint32_t)task_func_addr) | 1u;
    /* LR = exit handler */
    sp--; *sp = ((uint32_t)task_exit_handler_addr) | 1u;
    /* R12 */
    sp--; *sp = 0x00000000u;
    /* R3 = initial_lparam (upper half unused on M4, pass as R3:R2 pair) */
    sp--; *sp = (uint32_t)(initial_lparam >> 16);
    /* R2 = initial_lparam low */
    sp--; *sp = (uint32_t)(initial_lparam & 0xFFFFu);
    /* R1 = initial_sparam */
    sp--; *sp = (uint32_t)initial_sparam;
    /* R0 = initial_msg_type */
    sp--; *sp = (uint32_t)initial_msg_type;

    /* --- Software-saved frame (r4-r11) --- */
    sp--; *sp = 0x11111111u; /* R11 */
    sp--; *sp = 0x10101010u; /* R10 */
    sp--; *sp = 0x09090909u; /* R9 */
    sp--; *sp = 0x08080808u; /* R8 */
    sp--; *sp = 0x07070707u; /* R7 */
    sp--; *sp = 0x06060606u; /* R6 */
    sp--; *sp = 0x05050505u; /* R5 */
    sp--; *sp = 0x04040404u; /* R4 */

    return (void *)sp;
}

__attribute__((naked))
void ktos_hal_ContextSwitch(void **p_current_sp_storage,
                             const void *next_sp)
{
    __asm volatile (
        "push {r4-r11}      \n"
        "str  sp, [r0]      \n"
        "mov  sp, r1        \n"
        "pop  {r4-r11}      \n"
        "bx   lr            \n"
        ::: "memory"
    );
}

__attribute__((naked))
void ktos_hal_StartScheduler(const void *first_task_sp)
{
    __asm volatile (
        "ldr  r1, =OS_SP    \n"
        "mov  r2, sp        \n"
        "str  r2, [r1]      \n"
        "mov  sp, r0        \n"
        "pop  {r4-r11}      \n"
        "cpsie i            \n"
        "pop  {r0-r3,r12,lr}\n"
        "pop  {r0}          \n"
        "bx   lr            \n"
        ::: "memory"
    );
}

void ktos_hal_InitSystemTimer(void (*timer_isr_addr)(void)) {
    g_ktos_timer_isr = timer_isr_addr;
    uint32_t reload = (SYS_CLK_HZ / TICK_HZ) - 1UL;
    NVIC_SHPR3 |= (0xFFu << 24);
    SYST_CVR = 0u;
    SYST_RVR = reload;
    SYST_CSR = 0x07u;
}

void SysTick_Handler(void) {
    if (g_ktos_timer_isr) g_ktos_timer_isr();
}
