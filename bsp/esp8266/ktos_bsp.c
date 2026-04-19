/**
 * @file ktos_bsp.c
 * @brief KTOS Board Support Package — ESP8266 (Xtensa LX106) — partial
 *
 * Supported boards: NodeMCU, Wemos D1 Mini, ESP-01.
 *
 * | Property  | Value                              |
 * |-----------|------------------------------------|
 * | Core      | Xtensa LX106 32-bit               |
 * | RAM       | ~30 KB free (after WiFi stack)    |
 * | Flash     | 1 MB+                             |
 * | Clock     | 80 MHz                            |
 * | Toolchain | xtensa-lx106-elf-gcc (ESP8266 SDK)|
 *
 * ### Build
 * @code
 * xtensa-lx106-elf-gcc -mlongcalls ...
 * @endcode
 *
 * ### Status
 * - C parts (interrupts, timer, stack init): **complete**
 * - Assembly (ContextSwitch, StartScheduler): **TODO**
 *
 * ### Xtensa porting notes
 * The Xtensa LX106 uses a **windowed register** ABI (a0-a15).
 * - a0 = return address, a1 = stack pointer, a2-a7 = function arguments.
 * - Register windows rotate on @c ENTRY/@c RETW — context save must account
 *   for all live windows, not just the current one.
 * - Reference: *Xtensa ISA Reference Manual*, Chapter 4 (Register Windows).
 *
 * @defgroup ktos_bsp_xtensa KTOS BSP — Xtensa LX106 (ESP8266)
 * @ingroup  ktos_hal
 */

#include "../../core/ktos_hal.h"
#include <stdint.h>

/* =========================================================================
 * Interrupt control
 * ========================================================================= */

/** @brief Disable all interrupts (Xtensa @c RSIL a2, 15 — set level 15). */
void ktos_hal_DisableInterrupts(void)
{
    __asm volatile ("rsil a2, 15" ::: "a2", "memory");
}

/** @brief Re-enable interrupts (Xtensa @c RSIL a2, 0 — set level 0). */
void ktos_hal_EnableInterrupts(void)
{
    __asm volatile ("rsil a2, 0" ::: "a2", "memory");
}

/* =========================================================================
 * System timer — FRC1, 1 ms tick at 80 MHz
 * ========================================================================= */

#define FRC1_LOAD_ADDRESS   0x60000600UL
#define FRC1_COUNT_ADDRESS  0x60000604UL
#define FRC1_CTRL_ADDRESS   0x60000608UL
#define FRC1_INT_ADDRESS    0x6000060CUL

#define FRC1_CTRL_DIV_256   (3 << 2)
#define FRC1_CTRL_RELOAD    (1 << 6)
#define FRC1_CTRL_INT_EN    (1 << 7)
#define FRC1_TICKS_PER_MS   ((80000000UL / 256) / 1000)  /* ~312 at 80MHz */

/**
 * @brief Configure the FRC1 free-running counter for a 1 ms interrupt.
 *
 * Uses prescaler 256: 80 MHz / 256 / 1000 ≈ 312 ticks per ms.
 * Register the ISR manually after calling this function:
 * @code
 * ETS_FRC_TIMER1_INTR_ATTACH(ktos_timer_irq_handler, NULL);
 * ETS_FRC1_INTR_ENABLE();
 * @endcode
 *
 */
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

/* =========================================================================
 * Task stack initialisation
 * ========================================================================= */

/**
 * @brief Build the initial Xtensa LX106 stack frame for a new task.
 *
 * Xtensa windowed ABI layout (16-byte aligned, grows downward):
 * - PS register (processor state — interrupt level 0, WOE enabled)
 * - PC  (task entry address)
 * - a0  (return address → exit handler)
 * - a2  (MsgType — first argument)
 * - a2  (sParam)
 * - a2  (lParam)
 * - a3  (zeroed)
 *
 * @note The exact layout depends on the assembly implementation of
 *       ktos_hal_ContextSwitch().  Update both together when implementing.
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

/* =========================================================================
 * Context switch and scheduler launch — TODO: Xtensa assembly required
 * ========================================================================= */

/**
 * @brief Xtensa LX106 context switch. **TODO — assembly not yet implemented.**
 *
 * Implementation steps:
 * 1. ENTRY to create a new register window (or disable windowing first).
 * 2. Save a0-a15, SAR (shift amount register), and PS onto the current stack.
 * 3. Store current SP into @c *p_current_sp_storage.
 * 4. Load @p next_sp into a1 (Xtensa SP register).
 * 5. Restore a0-a15, SAR, and PS from the new stack.
 * 6. RETW (or equivalent) to jump into the new task.
 *
 * Reference: *Xtensa ISA Reference Manual*, Chapter 4 — Register Windows.
 *
 */
void ktos_hal_ContextSwitch(void **p_current_sp_storage, void *next_sp)
{
    /* TODO: implement in Xtensa assembly */
    (void)p_current_sp_storage;
    (void)next_sp;
}

/**
 * @brief Launch the first task.  **TODO — assembly not yet implemented.**
 *
 * Must set a1 (SP) to @p first_task_sp, restore the initial register context,
 * and jump to the task entry point without returning.
 *
 */
void ktos_hal_StartScheduler(void *first_task_sp)
{
    /* TODO: implement in Xtensa assembly */
    (void)first_task_sp;
    while (1);
}
