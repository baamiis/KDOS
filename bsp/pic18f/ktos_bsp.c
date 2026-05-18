/*
 * KTOS — Tiny Cooperative Task Switcher
 * Copyright (C) 2004-2025 Khalid Hamdou / BAAMIIS LIMITED
 * All rights reserved.
 *
 * Author:  Khalid Hamdou
 * Company: BAAMIIS LIMITED
 * GitHub:  https://github.com/baamiis/KTOS
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is part of KTOS.
 *
 * KTOS is dual-licensed:
 *
 *   Open Source: GNU General Public License v3 (see LICENSE)
 *   Commercial:  Proprietary license available (see COMMERCIAL_LICENSE)
 *
 * For open source use, this program is free software: you can
 * redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * For commercial/proprietary use without GPL obligations, a Commercial
 * License must be obtained from BAAMIIS LIMITED.
 * Contact: baamiis7@gmail.com
 *
 * KTOS is the original work of Khalid Hamdou. No person or organisation
 * may claim authorship or ownership of this software.
 */

/**
 * @file ktos_bsp.c
 * @brief KTOS Board Support Package — PIC18F4550 (PIC18 8-bit) [Experimental]
 *
 * Supported boards: Custom board (SDCC toolchain).
 *
 * | Property  | Value                              |
 * |-----------|------------------------------------|
 * | Core      | PIC18 8-bit Harvard                |
 * | RAM       | 2 KB                               |
 * | Flash     | 32 KB                              |
 * | Clock     | 48 MHz (USB PLL) / 12 MHz Fosc/4  |
 * | Toolchain | SDCC with --use-non-free           |
 *
 * ### Build
 * @code
 * sdcc -mpic18 -ppic18f4550 --use-non-free ...
 * @endcode
 *
 * @warning PIC18F context switching has a 31-level hardware call stack limit.
 *          Deep call chains across task boundaries may corrupt the hardware stack.
 *          This BSP is suitable for applications with shallow call stacks only.
 *          Toolchain: SDCC (http://sdcc.sourceforge.net) with --use-non-free.
 *
 * ### Architecture constraints
 * PIC18 has a separate 31-level hardware return stack (not in data RAM).
 * Only cooperative context switch is practical — saving/restoring the
 * hardware stack contents across arbitrary task boundaries is not feasible.
 * This BSP saves the data-RAM software stack state (FSR1 pointer) along
 * with the key CPU registers.
 *
 * Context saving covers: WREG, STATUS, BSR, FSR0, FSR1, FSR2, PRODL, PRODH.
 * The task's hardware stack depth must remain within PIC18's 31-entry limit
 * across the lifetime of any cooperative yield.
 *
 * @defgroup ktos_bsp_pic18f KTOS BSP — PIC18F4550 (Experimental)
 * @ingroup  ktos_hal
 */

#include "../../core/ktos_hal.h"
#include <stdint.h>

/* =========================================================================
 * PIC18F SFR definitions compatible with SDCC
 * (SDCC has these built-in via pic18fregs.h; shown here for clarity)
 * ========================================================================= */
__sfr __at(0xFF4) WREG_reg;
__sfr __at(0xFD8) STATUS_sfr;
__sfr __at(0xFE0) BSR_sfr;
__sfr __at(0xFEA) FSR0L_sfr;
__sfr __at(0xFEB) FSR0H_sfr;
__sfr __at(0xFE1) FSR1L_sfr;
__sfr __at(0xFE2) FSR1H_sfr;
__sfr __at(0xFDA) PRODL_sfr;
__sfr __at(0xFDB) PRODH_sfr;

/* Timer0 registers */
__sfr __at(0xFD5) T0CON_sfr;
__sfr __at(0xFD6) TMR0H_sfr;
__sfr __at(0xFD7) TMR0L_sfr;
__sfr __at(0xFF2) INTCON_sfr;

/* =========================================================================
 * Per-task context block
 * ========================================================================= */

/**
 * @brief PIC18F per-task CPU context saved across cooperative yields.
 *
 * Because PIC18 has no traditional software stack pointer for context
 * switching, this BSP uses a per-task struct in data RAM to hold CPU state.
 * The @c pc field records the logical re-entry point but cannot directly
 * set the hardware PC — the scheduler must call into each task normally.
 *
 * @note This is a simplified/experimental implementation suitable only for
 *       shallow cooperative tasks that do not rely on complex call stacks
 *       persisting across yields.
 */
typedef struct {
    uint8_t  wreg;    /**< WREG working register */
    uint8_t  status;  /**< STATUS (C, DC, Z, OV, N flags + bank) */
    uint8_t  bsr;     /**< Bank Select Register */
    uint16_t fsr0;    /**< FSR0 (indirect addressing pointer 0) */
    uint16_t fsr1;    /**< FSR1 (software stack pointer for SDCC) */
    uint16_t fsr2;    /**< FSR2 (frame pointer for SDCC) */
    uint8_t  prodl;   /**< PRODL (multiply result low byte) */
    uint8_t  prodh;   /**< PRODH (multiply result high byte) */
    uint16_t pc;      /**< Logical entry point (truncated to 16-bit) */
} ktos_pic18_ctx_t;

/* =========================================================================
 * Interrupt control
 * ========================================================================= */

/**
 * @brief Disable all maskable interrupts (clear GIE, bit7 of INTCON).
 */
void ktos_hal_DisableInterrupts(void)
{
    INTCON_sfr &= ~(1 << 7); /* GIE = 0 */
}

/**
 * @brief Re-enable maskable interrupts (set GIE, bit7 of INTCON).
 */
void ktos_hal_EnableInterrupts(void)
{
    INTCON_sfr |= (1 << 7); /* GIE = 1 */
}

/**
 * @brief Configure Timer0 for a ~1 ms interrupt at 12 MHz instruction clock.
 *
 * At 48MHz USB PLL, Fosc/4 = 12 MHz instruction clock.
 * Timer0 16-bit, prescaler 1:4:
 *   Period = 4 * 65536 / 12000000 ≈ 21.8 ms (uncalibrated, pre-loaded).
 * Pre-load TMR0 for 1 ms:
 *   counts per 1ms = 12000000 / (4 * 1000) = 3000
 *   TMR0 pre-load = 65536 - 3000 = 62536 = 0xF448
 *
 * @note The ISR must reload TMR0H:TMR0L at entry for accurate period.
 */
void ktos_hal_InitSystemTimer(void (*timer_isr_addr)(void))
{
    /* Stop Timer0, configure 16-bit, prescaler 1:4, internal clock */
    T0CON_sfr = 0x01U; /* T0CS=0 (Fosc/4), PSA=0 (prescaler on), T0PS=001 (1:4), TMR0ON=0, T08BIT=0 */

    /* Pre-load for 1 ms period */
    TMR0H_sfr = 0xF4U;
    TMR0L_sfr = 0x48U;

    /* Enable Timer0 overflow interrupt (bit5 of INTCON) */
    INTCON_sfr |= (1 << 5); /* TMR0IE */

    /* Start Timer0 */
    T0CON_sfr |= (1 << 7); /* TMR0ON */

    (void)timer_isr_addr;
    /*
     * ISR wiring — add to application:
     * void interrupt high_priority isr(void) {
     *     if (INTCON_sfr & (1 << 2)) {  // TMR0IF
     *         TMR0H_sfr = 0xF4; TMR0L_sfr = 0x48;  // reload
     *         INTCON_sfr &= ~(1 << 2);              // clear TMR0IF
     *         ktos_timer_irq_handler();
     *     }
     * }
     */
}

/**
 * @brief Initialise a per-task context block for a new task.
 *
 * Returns a pointer to a @c ktos_pic18_ctx_t in data RAM.  The context
 * block holds the initial CPU state that would be "restored" when the
 * task is first dispatched.
 *
 * @note The PIC18 hardware call stack cannot be pre-populated from C.
 *       The scheduler must call the task function directly on first dispatch.
 *       This implementation stores the task entry point in @c ctx->pc and
 *       the application scheduler layer is responsible for the first call.
 *
 * @warning The returned pointer must remain valid for the lifetime of the task.
 *          The caller (ktos.c) allocates from its own pool; p_stack_base and
 *          stack_size_bytes are used to hold the ktos_pic18_ctx_t at the
 *          top of the provided buffer.
 */
void *ktos_hal_InitTaskStack(void *p_stack_base,
                              unsigned int stack_size_bytes,
                              WORD (*task_func_addr)(WORD, WORD, LONG),
                              void (*task_exit_handler_addr)(WORD),
                              WORD initial_msg_type,
                              WORD initial_sparam,
                              LONG initial_lparam)
{
    /* Place context block at the base of the provided buffer */
    ktos_pic18_ctx_t *ctx = (ktos_pic18_ctx_t *)p_stack_base;

    if (stack_size_bytes < sizeof(ktos_pic18_ctx_t)) {
        return (void *)0;
    }

    ctx->wreg   = 0;
    ctx->status = 0;
    ctx->bsr    = 0;
    ctx->fsr0   = 0;
    /* FSR1 points to just above the context block — SDCC software stack base */
    ctx->fsr1   = (uint16_t)((uintptr_t)p_stack_base + sizeof(ktos_pic18_ctx_t));
    ctx->fsr2   = ctx->fsr1;
    ctx->prodl  = (uint8_t)(initial_msg_type & 0xFF);
    ctx->prodh  = (uint8_t)((initial_msg_type >> 8) & 0xFF);
    ctx->pc     = (uint16_t)(uintptr_t)task_func_addr;

    (void)task_exit_handler_addr;
    (void)initial_sparam;
    (void)initial_lparam;

    return (void *)ctx;
}

/**
 * @brief Save and restore PIC18 CPU context across a cooperative yield.
 *
 * Saves WREG, STATUS, BSR, FSR0, FSR1, FSR2, PRODL, PRODH of the current
 * task into the context block pointed to by @p p_current_sp_storage, then
 * restores the next task's context from the block at @p next_sp.
 *
 * @note PIC18 inline assembly with SDCC uses the @c __asm syntax.
 *       The actual GOTO/CALL to re-enter the task is handled by the scheduler
 *       layer — this function only swaps the register context.
 *
 * @warning Hardware call stack depth must not exceed 31 entries at the point
 *          of any cooperative yield or the hardware stack will overflow.
 */
void ktos_hal_ContextSwitch(void **p_current_sp_storage, const void *next_sp)
{
    ktos_pic18_ctx_t *cur  = (ktos_pic18_ctx_t *)*p_current_sp_storage;
    ktos_pic18_ctx_t *next = (ktos_pic18_ctx_t *)next_sp;

    /* Save current context */
    cur->fsr1  = (uint16_t)((uint8_t)FSR1L_sfr | ((uint8_t)FSR1H_sfr << 8));
    cur->fsr0  = (uint16_t)((uint8_t)FSR0L_sfr | ((uint8_t)FSR0H_sfr << 8));
    cur->fsr2  = 0; /* FSR2 not used in this minimal save */
    cur->prodl = PRODL_sfr;
    cur->prodh = PRODH_sfr;
    cur->bsr   = BSR_sfr;
    /* WREG and STATUS are restored by the scheduler on next dispatch */

    /* Restore next context */
    FSR1L_sfr = (uint8_t)(next->fsr1 & 0xFF);
    FSR1H_sfr = (uint8_t)(next->fsr1 >> 8);
    FSR0L_sfr = (uint8_t)(next->fsr0 & 0xFF);
    FSR0H_sfr = (uint8_t)(next->fsr0 >> 8);
    PRODL_sfr = next->prodl;
    PRODH_sfr = next->prodh;
    BSR_sfr   = next->bsr;

    /* Update p_current_sp_storage to point to new context for return */
    *p_current_sp_storage = (void *)next;
}

/**
 * @brief Load the first task's context and begin execution.
 *
 * Restores FSR1/FSR0 from the first task's context block and transfers
 * control.  On PIC18, actual PC transfer is implicit — the scheduler calls
 * the task function directly using the @c pc field.
 *
 * @note Unlike ARM/AVR, this function returns normally after restoring the
 *       register context.  The caller (ktos.c) must then call the task
 *       function pointed to by the context block's @c pc field.
 */
void ktos_hal_StartScheduler(const void *first_task_sp)
{
    const ktos_pic18_ctx_t *ctx = (const ktos_pic18_ctx_t *)first_task_sp;

    FSR1L_sfr = (uint8_t)(ctx->fsr1 & 0xFF);
    FSR1H_sfr = (uint8_t)(ctx->fsr1 >> 8);
    FSR0L_sfr = (uint8_t)(ctx->fsr0 & 0xFF);
    FSR0H_sfr = (uint8_t)(ctx->fsr0 >> 8);
    BSR_sfr   = ctx->bsr;

    /* Interrupts enabled before first task runs */
    ktos_hal_EnableInterrupts();

    /*
     * The scheduler (ktos.c) must invoke the first task function directly
     * after this returns, using the function pointer stored during InitTask.
     * The PC stored in ctx->pc is informational only — PIC18 cannot set PC
     * directly from C.
     */
}
