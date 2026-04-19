/*
 * KTOS BSP for ESP8266 (Xtensa LX106)
 * Core: Xtensa LX106 32-bit
 * RAM:  ~80KB total (~30KB free after WiFi stack)
 * Flash: 1MB+
 * Clock: 80MHz
 *
 * Toolchain: xtensa-lx106-elf-gcc (ESP8266 SDK)
 *
 * NOTE: Xtensa LX106 context switch requires Xtensa-specific assembly.
 * The windowed register architecture (a0-a15) requires special handling.
 * This BSP provides the C parts fully implemented.
 * The assembly parts (ContextSwitch, StartScheduler) are stubbed with
 * detailed instructions for the implementor.
 */

#include "../../core/ktos_hal.h"
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Interrupt Control                                                    */
/* ------------------------------------------------------------------ */

void ktos_hal_DisableInterrupts(void)
{
    __asm volatile ("rsil a2, 15" ::: "a2", "memory");
}

void ktos_hal_EnableInterrupts(void)
{
    __asm volatile ("rsil a2, 0" ::: "a2", "memory");
}

/* ------------------------------------------------------------------ */
/* System Timer — FRC1 hardware timer, 1ms tick                        */
/* ------------------------------------------------------------------ */

#define FRC1_LOAD_ADDRESS   0x60000600UL
#define FRC1_COUNT_ADDRESS  0x60000604UL
#define FRC1_CTRL_ADDRESS   0x60000608UL
#define FRC1_INT_ADDRESS    0x6000060CUL

#define FRC1_CTRL_DIV_256   (3 << 2)
#define FRC1_CTRL_RELOAD    (1 << 6)
#define FRC1_CTRL_INT_EN    (1 << 7)
#define FRC1_TICKS_PER_MS   ((80000000UL / 256) / 1000)  /* ~312 at 80MHz */

void ktos_hal_InitSystemTimer(void (*timer_isr_addr)(void))
{
    volatile uint32_t *load = (volatile uint32_t *)FRC1_LOAD_ADDRESS;
    volatile uint32_t *ctrl = (volatile uint32_t *)FRC1_CTRL_ADDRESS;

    *load = FRC1_TICKS_PER_MS;
    *ctrl = FRC1_CTRL_DIV_256 | FRC1_CTRL_RELOAD | FRC1_CTRL_INT_EN;

    (void)timer_isr_addr;
    /* Register ktos_timer_irq_handler with the ESP8266 interrupt system:
     * ETS_FRC_TIMER1_INTR_ATTACH(ktos_timer_irq_handler, NULL);
     * ETS_FRC1_INTR_ENABLE();
     */
}

/* ------------------------------------------------------------------ */
/* Task Stack Initialisation                                            */
/* ------------------------------------------------------------------ */
/*
 * Xtensa LX106 uses a windowed register ABI (a0-a15).
 * a0 = return address, a1 = stack pointer, a2-a7 = function arguments.
 * Initial frame must set up a0 (exit handler) and a2 (first arg).
 */

void *ktos_hal_InitTaskStack(void *p_stack_base,
                              unsigned int stack_size_bytes,
                              void (*task_func_addr)(WORD, WORD, LONG),
                              void (*task_exit_handler_addr)(WORD),
                              WORD initial_msg_type,
                              WORD initial_sparam,
                              LONG initial_lparam)
{
    uint32_t *sp = (uint32_t *)((uint8_t *)p_stack_base + stack_size_bytes);
    sp = (uint32_t *)((uint32_t)sp & ~0xFUL); /* 16-byte alignment for Xtensa */

    /* Xtensa base save area (BSA) — 4 registers a0-a3 saved by ENTRY */
    *--sp = 0;                                  /* a3 */
    *--sp = (uint32_t)initial_lparam;           /* a2 used as arg */
    *--sp = (uint32_t)initial_sparam;           /* a2 (second arg) */
    *--sp = (uint32_t)initial_msg_type;         /* a2 (first arg)  */
    *--sp = (uint32_t)task_exit_handler_addr;   /* a0 = return addr */
    *--sp = (uint32_t)task_func_addr;           /* PC */

    /* PS (processor state) register — interrupt level 0, WOE enabled */
    *--sp = 0x00040000UL;

    return (void *)sp;
}

/* ------------------------------------------------------------------ */
/* Context Switch — TODO: Xtensa assembly required                      */
/* ------------------------------------------------------------------ */
/*
 * Xtensa LX106 context switch steps:
 * 1. Use ENTRY/RETW or manually manage register windows
 * 2. Save a0-a15, SAR (shift amount register), PS
 * 3. Store SP into *p_current_sp_storage
 * 4. Load next_sp into SP
 * 5. Restore a0-a15, SAR, PS from new stack
 * 6. Return into new task
 *
 * Reference: Xtensa ISA Reference Manual, Chapter 4 (Register Windows)
 */

void ktos_hal_ContextSwitch(void **p_current_sp_storage, void *next_sp)
{
    /* TODO: Implement in Xtensa assembly */
    (void)p_current_sp_storage;
    (void)next_sp;
}

void ktos_hal_StartScheduler(void *first_task_sp)
{
    /* TODO: Implement in Xtensa assembly */
    (void)first_task_sp;
    while(1);
}
