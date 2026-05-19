/*
 * KTOS — SAM3X8E (Arduino Due) minimal bare-metal startup
 *
 * Provides:
 *   - Vector table (.vectors section, placed at 0x00080000 by sam3x8e.ld)
 *   - Reset_Handler: copies .data, zeros .bss, calls main()
 *   - Default_Handler: infinite loop for unhandled exceptions
 *   - TC0_Handler: declared weak — application main.c overrides it
 *
 * TC0 channel 0 (IRQ 27) is the KTOS 1 ms tick source.  Every main.c
 * that uses KTOS must define:
 *
 *   void TC0_Handler(void) {
 *       (void)(*(volatile uint32_t *)0x40080020UL);  // clear TC_SR flag
 *       ktos_timer_irq_handler();
 *   }
 */

#include <stdint.h>

/* Linker script symbols */
extern uint32_t _sidata;   /* load address of .data in flash */
extern uint32_t _sdata;    /* start of .data in SRAM */
extern uint32_t _edata;    /* end of .data in SRAM */
extern uint32_t _sbss;
extern uint32_t _ebss;
extern uint32_t _estack;   /* initial SP = top of SRAM */

extern int main(void);

/* =========================================================================
 * Default handler — loops forever on unhandled exceptions
 * ========================================================================= */

void Default_Handler(void) { while (1) {} }

/* =========================================================================
 * Weak aliases — application main.c may override any of these
 * ========================================================================= */

void NMI_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void)      __attribute__((weak, alias("Default_Handler")));

/* SAM3X8E peripheral IRQs */
void SUPC_Handler(void)         __attribute__((weak, alias("Default_Handler")));
void RSTC_Handler(void)         __attribute__((weak, alias("Default_Handler")));
void RTC_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void RTT_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void WDT_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void PMC_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void EFC0_Handler(void)         __attribute__((weak, alias("Default_Handler")));
void EFC1_Handler(void)         __attribute__((weak, alias("Default_Handler")));
void UART_Handler(void)         __attribute__((weak, alias("Default_Handler")));
void SMC_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void PIOA_Handler(void)         __attribute__((weak, alias("Default_Handler")));
void PIOB_Handler(void)         __attribute__((weak, alias("Default_Handler")));
void PIOC_Handler(void)         __attribute__((weak, alias("Default_Handler")));
void PIOD_Handler(void)         __attribute__((weak, alias("Default_Handler")));
void USART0_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void USART1_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void USART2_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void USART3_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void HSMCI_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void TWI0_Handler(void)         __attribute__((weak, alias("Default_Handler")));
void TWI1_Handler(void)         __attribute__((weak, alias("Default_Handler")));
void SPI0_Handler(void)         __attribute__((weak, alias("Default_Handler")));
void SSC_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void TC0_Handler(void)          __attribute__((weak, alias("Default_Handler"))); /* KTOS tick */
void TC1_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void TC2_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void TC3_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void TC4_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void TC5_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void TC6_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void TC7_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void TC8_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void PWM_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void ADC_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void DACC_Handler(void)         __attribute__((weak, alias("Default_Handler")));
void DMAC_Handler(void)         __attribute__((weak, alias("Default_Handler")));
void UOTGHS_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void TRNG_Handler(void)         __attribute__((weak, alias("Default_Handler")));
void EMAC_Handler(void)         __attribute__((weak, alias("Default_Handler")));
void CAN0_Handler(void)         __attribute__((weak, alias("Default_Handler")));
void CAN1_Handler(void)         __attribute__((weak, alias("Default_Handler")));

/* =========================================================================
 * Vector table — must be at 0x00080000 (start of Due application flash)
 * ========================================================================= */

typedef void (*isr_t)(void);

__attribute__((section(".vectors"), used))
static const isr_t vector_table[] = {
    (isr_t)&_estack,       /* 0x00: Initial SP */
    (isr_t)Reset_Handler,  /* 0x04: Reset */
    NMI_Handler,           /* 0x08: NMI */
    HardFault_Handler,     /* 0x0C: HardFault */
    MemManage_Handler,     /* 0x10: MemManage */
    BusFault_Handler,      /* 0x14: BusFault */
    UsageFault_Handler,    /* 0x18: UsageFault */
    0, 0, 0, 0,            /* Reserved */
    SVC_Handler,           /* SVC */
    DebugMon_Handler,      /* Debug Monitor */
    0,                     /* Reserved */
    PendSV_Handler,        /* PendSV */
    SysTick_Handler,       /* SysTick */
    /* External IRQs 0-44 */
    SUPC_Handler,          /* 0 */
    RSTC_Handler,          /* 1 */
    RTC_Handler,           /* 2 */
    RTT_Handler,           /* 3 */
    WDT_Handler,           /* 4 */
    PMC_Handler,           /* 5 */
    EFC0_Handler,          /* 6 */
    EFC1_Handler,          /* 7 */
    UART_Handler,          /* 8 */
    SMC_Handler,           /* 9 */
    0,                     /* 10: reserved */
    PIOA_Handler,          /* 11 */
    PIOB_Handler,          /* 12 */
    PIOC_Handler,          /* 13 */
    PIOD_Handler,          /* 14 */
    0,                     /* 15: reserved */
    0,                     /* 16: reserved */
    USART0_Handler,        /* 17 */
    USART1_Handler,        /* 18 */
    USART2_Handler,        /* 19 */
    USART3_Handler,        /* 20 */
    HSMCI_Handler,         /* 21 */
    TWI0_Handler,          /* 22 */
    TWI1_Handler,          /* 23 */
    SPI0_Handler,          /* 24 */
    0,                     /* 25: reserved */
    SSC_Handler,           /* 26 */
    TC0_Handler,           /* 27 — KTOS 1 ms tick */
    TC1_Handler,           /* 28 */
    TC2_Handler,           /* 29 */
    TC3_Handler,           /* 30 */
    TC4_Handler,           /* 31 */
    TC5_Handler,           /* 32 */
    TC6_Handler,           /* 33 */
    TC7_Handler,           /* 34 */
    TC8_Handler,           /* 35 */
    PWM_Handler,           /* 36 */
    ADC_Handler,           /* 37 */
    DACC_Handler,          /* 38 */
    DMAC_Handler,          /* 39 */
    UOTGHS_Handler,        /* 40 */
    TRNG_Handler,          /* 41 */
    EMAC_Handler,          /* 42 */
    CAN0_Handler,          /* 43 */
    CAN1_Handler,          /* 44 */
};

/* =========================================================================
 * Reset handler — copies .data, zeros .bss, calls main()
 * ========================================================================= */

void Reset_Handler(void)
{
    /* Copy initialised data from flash to SRAM */
    uint32_t *src = &_sidata;
    for (uint32_t *dst = &_sdata; dst < &_edata; ) {
        *dst++ = *src++;
    }

    /* Zero BSS */
    for (uint32_t *p = &_sbss; p < &_ebss; ) {
        *p++ = 0;
    }

    main();
    while (1) {}
}
