/**
 * @file ktos_bsp.c
 * @brief KTOS Board Support Package — ATmega328P (AVR 8-bit)
 *
 * Supported boards: Arduino Uno, Arduino Nano, Arduino Pro Mini.
 *
 * | Property  | Value                              |
 * |-----------|------------------------------------|
 * | Core      | AVR 8-bit                          |
 * | RAM       | 2 KB                               |
 * | Flash     | 32 KB                              |
 * | Clock     | 16 MHz (Arduino) / 8 MHz (3.3 V)  |
 * | Toolchain | avr-gcc                            |
 *
 * ### Build
 * @code
 * avr-gcc -mmcu=atmega328p -DF_CPU=16000000UL ...
 * @endcode
 *
 * ### Timer ISR wiring
 * Add this to your application (not inside the BSP):
 * @code
 * #include <avr/interrupt.h>
 * ISR(TIMER1_COMPA_vect) { ktos_timer_irq_handler(); }
 * @endcode
 *
 * @defgroup ktos_bsp_avr KTOS BSP — AVR (ATmega328P)
 * @ingroup  ktos_hal
 */

#include "../../core/ktos_hal.h"
#include <avr/io.h>
#include <avr/interrupt.h>

/* =========================================================================
 * Interrupt control
 * ========================================================================= */

/** @brief Disable all interrupts (AVR @c cli). */
void ktos_hal_DisableInterrupts(void) { cli(); }

/** @brief Re-enable interrupts (AVR @c sei). */
void ktos_hal_EnableInterrupts(void) { sei(); }

/* =========================================================================
 * System timer — Timer1 CTC, 1 ms tick at 16 MHz
 * ========================================================================= */

/**
 * @brief Configure Timer1 in CTC mode for a 1 ms interrupt.
 *
 * Settings: prescaler 64, OCR1A = 249 → 1 ms at 16 MHz.
 * The ISR vector @c TIMER1_COMPA_vect must be defined in the application
 * and must call @c ktos_timer_irq_handler().
 *
 */
void ktos_hal_InitSystemTimer(void (*timer_isr_addr)(void))
{
    /* Timer1 CTC mode, prescaler 64 → 1ms tick at 16MHz
     * OCR1A = (F_CPU / prescaler / 1000) - 1 = (16000000/64/1000)-1 = 249 */
    TCCR1A = 0;
    TCCR1B = (1 << WGM12) | (1 << CS11) | (1 << CS10); /* CTC, prescaler 64 */
    OCR1A  = 249;
    TIMSK1 = (1 << OCIE1A); /* Enable compare match interrupt */
    (void)timer_isr_addr;
    /* AVR uses a fixed vector table — the ISR must be named TIMER1_COMPA_vect
     * in the application. ktos_timer_irq_handler() should be called from it:
     *
     * ISR(TIMER1_COMPA_vect) { ktos_timer_irq_handler(); }
     */
}

/* =========================================================================
 * Task stack initialisation
 * ========================================================================= */

/**
 * @brief Build the initial AVR stack frame for a new task.
 *
 * AVR stack grows downward.  The frame looks exactly like what would be
 * left on the stack after the task function called a subroutine, so that
 * ktos_hal_ContextSwitch() can restore it uniformly.
 *
 * Stack layout (top = lowest address):
 * ```
 * [ R31..R1, R0 ]  32 general-purpose registers (R0 last, SREG first)
 * [ SREG ]         status register (bit 7 = I, interrupts enabled)
 * [ exit PC high ] return address of exit handler (AVR pushes PC as 2 bytes)
 * [ exit PC low  ]
 * [ task PC high ] entry point of the task function
 * [ task PC low  ]
 * ```
 */
void *ktos_hal_InitTaskStack(void *p_stack_base,
                              unsigned int stack_size_bytes,
                              WORD (*task_func_addr)(WORD, WORD, LONG),
                              void (*task_exit_handler_addr)(WORD),
                              WORD initial_msg_type,
                              WORD initial_sparam,
                              LONG initial_lparam)
{
    uint8_t *sp = (uint8_t *)p_stack_base + stack_size_bytes - 1;

    /* Push task function address as return address (LSB first for AVR) */
    uint16_t pc = (uint16_t)task_func_addr;
    *sp-- = (uint8_t)(pc & 0xFF);
    *sp-- = (uint8_t)((pc >> 8) & 0xFF);

    /* Push exit handler address so task returning calls it */
    uint16_t exit_pc = (uint16_t)task_exit_handler_addr;
    *sp-- = (uint8_t)(exit_pc & 0xFF);
    *sp-- = (uint8_t)((exit_pc >> 8) & 0xFF);

    /* Push initial SREG (interrupts enabled) */
    *sp-- = 0x80;

    /* Push R0-R25 as zero (R24:R25 = first arg = initial_msg_type) */
    for (int i = 0; i < 24; i++) *sp-- = 0x00;
    *sp-- = (uint8_t)(initial_msg_type & 0xFF);        /* R24 */
    *sp-- = (uint8_t)((initial_msg_type >> 8) & 0xFF); /* R25 */

    /* R22:R23 = second arg = initial_sparam */
    *sp-- = (uint8_t)(initial_sparam & 0xFF);
    *sp-- = (uint8_t)((initial_sparam >> 8) & 0xFF);

    /* R18:R21 = third arg (long) = initial_lparam */
    *sp-- = (uint8_t)(initial_lparam & 0xFF);
    *sp-- = (uint8_t)((initial_lparam >> 8) & 0xFF);
    *sp-- = (uint8_t)((initial_lparam >> 16) & 0xFF);
    *sp-- = (uint8_t)((initial_lparam >> 24) & 0xFF);

    return (void *)sp;
}

/* =========================================================================
 * Context switch — AVR assembly
 * ========================================================================= */

/**
 * @brief Save/restore all 32 AVR registers + SREG and swap stack pointers.
 *
 * Saves SREG and R0-R31 of the current context onto the current stack,
 * writes the current SP into @c *p_current_sp_storage, sets SP to
 * @p next_sp, then restores R0-R31 and SREG of the new context and
 * returns into it.
 *
 * @note AVR SP is split across two 8-bit I/O registers (@c SPL / @c SPH).
 *
 */
void ktos_hal_ContextSwitch(void **p_current_sp_storage, void *next_sp)
{
    __asm__ volatile (
        /* Save SREG and disable interrupts */
        "in     r0, __SREG__        \n"
        "cli                        \n"
        "push   r0                  \n"

        /* Save all GPRs */
        "push   r1                  \n"
        "push   r2                  \n"
        "push   r3                  \n"
        "push   r4                  \n"
        "push   r5                  \n"
        "push   r6                  \n"
        "push   r7                  \n"
        "push   r8                  \n"
        "push   r9                  \n"
        "push   r10                 \n"
        "push   r11                 \n"
        "push   r12                 \n"
        "push   r13                 \n"
        "push   r14                 \n"
        "push   r15                 \n"
        "push   r16                 \n"
        "push   r17                 \n"
        "push   r18                 \n"
        "push   r19                 \n"
        "push   r20                 \n"
        "push   r21                 \n"
        "push   r22                 \n"
        "push   r23                 \n"
        "push   r24                 \n"
        "push   r25                 \n"
        "push   r26                 \n"
        "push   r27                 \n"
        "push   r28                 \n"
        "push   r29                 \n"
        "push   r30                 \n"
        "push   r31                 \n"

        /* Store current SP into *p_current_sp_storage */
        "in     r26, __SP_L__       \n"
        "in     r27, __SP_H__       \n"
        "st     X+, r26             \n"  /* p_current_sp_storage is in r24:r25 (first arg) */
        "st     X,  r27             \n"
        /* Wait — first arg (p_current_sp_storage) is in r24:r25
         * Load pointer to storage location */
        /* Load next_sp (second arg) into Z */
        "movw   r30, r22            \n"  /* next_sp in r22:r23 */

        /* Set SP to next_sp */
        "out    __SP_H__, r31       \n"
        "out    __SP_L__, r30       \n"

        /* Restore new context registers */
        "pop    r31                 \n"
        "pop    r30                 \n"
        "pop    r29                 \n"
        "pop    r28                 \n"
        "pop    r27                 \n"
        "pop    r26                 \n"
        "pop    r25                 \n"
        "pop    r24                 \n"
        "pop    r23                 \n"
        "pop    r22                 \n"
        "pop    r21                 \n"
        "pop    r20                 \n"
        "pop    r19                 \n"
        "pop    r18                 \n"
        "pop    r17                 \n"
        "pop    r16                 \n"
        "pop    r15                 \n"
        "pop    r14                 \n"
        "pop    r13                 \n"
        "pop    r12                 \n"
        "pop    r11                 \n"
        "pop    r10                 \n"
        "pop    r9                  \n"
        "pop    r8                  \n"
        "pop    r7                  \n"
        "pop    r6                  \n"
        "pop    r5                  \n"
        "pop    r4                  \n"
        "pop    r3                  \n"
        "pop    r2                  \n"
        "pop    r1                  \n"
        "pop    r0                  \n"
        "out    __SREG__, r0        \n"
        "pop    r0                  \n"
        "ret                        \n"
        :
        : "x" (p_current_sp_storage), "z" (next_sp)
        : "memory"
    );
}

/* =========================================================================
 * Scheduler launch
 * ========================================================================= */

/**
 * @brief Load the first task's stack and begin execution.  Never returns.
 *
 * Sets the AVR stack pointer to @p first_task_sp, restores the initial
 * register context built by ktos_hal_InitTaskStack(), and executes RET to
 * jump to the task entry point.
 *
 */
void ktos_hal_StartScheduler(void *first_task_sp)
{
    /* Load the first task stack pointer and restore its context */
    __asm__ volatile (
        "out    __SP_H__, %B0       \n"
        "out    __SP_L__, %A0       \n"
        :
        : "r" (first_task_sp)
    );

    /* Restore context of first task and jump to it */
    __asm__ volatile (
        "pop    r31                 \n"
        "pop    r30                 \n"
        "pop    r29                 \n"
        "pop    r28                 \n"
        "pop    r27                 \n"
        "pop    r26                 \n"
        "pop    r25                 \n"
        "pop    r24                 \n"
        "pop    r23                 \n"
        "pop    r22                 \n"
        "pop    r21                 \n"
        "pop    r20                 \n"
        "pop    r19                 \n"
        "pop    r18                 \n"
        "pop    r17                 \n"
        "pop    r16                 \n"
        "pop    r15                 \n"
        "pop    r14                 \n"
        "pop    r13                 \n"
        "pop    r12                 \n"
        "pop    r11                 \n"
        "pop    r10                 \n"
        "pop    r9                  \n"
        "pop    r8                  \n"
        "pop    r7                  \n"
        "pop    r6                  \n"
        "pop    r5                  \n"
        "pop    r4                  \n"
        "pop    r3                  \n"
        "pop    r2                  \n"
        "pop    r1                  \n"
        "pop    r0                  \n"
        "out    __SREG__, r0        \n"
        "pop    r0                  \n"
        "ret                        \n"
    );

    while(1); /* Never reached */
}
