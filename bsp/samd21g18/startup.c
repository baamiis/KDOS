/*
 * KTOS — ATSAMD21G18 (Arduino Zero) minimal bare-metal startup
 *
 * Provides:
 *   - Vector table (.vectors at 0x00002000, after 8 KB bootloader)
 *   - Reset_Handler: 48 MHz clock init, copy .data, zero .bss, call main()
 *   - Default_Handler: infinite loop for unhandled exceptions
 *   - TC3_Handler: declared weak — application main.c overrides it
 *
 * Clock setup: XOSC32K → GCLK1 → DFLL48M closed-loop → GCLK0 = 48 MHz.
 * TC3 (IRQ 21) is the KTOS 1 ms tick source.
 *
 * Every main.c must define:
 *   void TC3_Handler(void) {
 *       *(volatile uint8_t *)0x42002C0EUL = 1;  // clear TC3 INTFLAG.OVF
 *       ktos_timer_irq_handler();
 *   }
 */

#include <stdint.h>

/* Linker script symbols */
extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;
extern uint32_t _estack;
extern int main(void);

/* =========================================================================
 * SAMD21 register shortcuts used only in startup
 * ========================================================================= */
#define NVMCTRL_CTRLB    (*(volatile uint32_t *)0x41004004UL)
#define SYSCTRL_PCLKSR   (*(volatile uint32_t *)0x40000804UL)
#define SYSCTRL_XOSC32K  (*(volatile uint32_t *)0x40000814UL)
#define SYSCTRL_OSC8M    (*(volatile uint32_t *)0x40000820UL)
#define SYSCTRL_DFLLCTRL (*(volatile uint16_t *)0x40000824UL)
#define SYSCTRL_DFLLVAL  (*(volatile uint32_t *)0x40000828UL)
#define SYSCTRL_DFLLMUL  (*(volatile uint32_t *)0x4000082CUL)
#define SYSCTRL_DFLLSYNC (*(volatile uint8_t  *)0x40000830UL)
#define GCLK_CTRL        (*(volatile uint8_t  *)0x40000C00UL)
#define GCLK_STATUS      (*(volatile uint8_t  *)0x40000C01UL)
#define GCLK_CLKCTRL     (*(volatile uint16_t *)0x40000C02UL)
#define GCLK_GENCTRL     (*(volatile uint32_t *)0x40000C04UL)
#define GCLK_GENDIV      (*(volatile uint32_t *)0x40000C08UL)
#define SCB_VTOR         (*(volatile uint32_t *)0xE000ED08UL)

static void clock_init_48MHz(void)
{
    /* 1 NVM wait state required above 24 MHz */
    NVMCTRL_CTRLB = (NVMCTRL_CTRLB & ~(0xFu << 1)) | (1u << 1);

    /* Enable XOSC32K crystal (32.768 kHz), 1 ms startup */
    SYSCTRL_XOSC32K = (1u<<1)|(1u<<2)|(1u<<3)|(4u<<8); /* EN32K|XTALEN|STARTUP=4 */
    SYSCTRL_XOSC32K |= (1u<<1); /* re-write with ENABLE bit set (bit 1 = ENABLE) */

    /* Wait for XOSC32K ready */
    while (!(SYSCTRL_PCLKSR & (1u << 0))) {}

    /* GCLK1 = XOSC32K (32.768 kHz reference for DFLL) */
    GCLK_GENDIV  = (1u << 0);                              /* GCLK1 divisor = 1 */
    while (GCLK_STATUS & (1u << 7)) {}
    GCLK_GENCTRL = (1u << 0) | (5u << 8) | (1u << 16);    /* ID=1|SRC=XOSC32K|GCLKEN */
    while (GCLK_STATUS & (1u << 7)) {}

    /* Connect GCLK1 to DFLL48M reference (peripheral clock ID 0) */
    GCLK_CLKCTRL = (uint16_t)((1u << 8) | (1u << 14)); /* ID=0|GEN=1|CLKEN */
    while (GCLK_STATUS & (1u << 7)) {}

    /* Enable DFLL48M in open-loop first (required before switching to closed-loop) */
    SYSCTRL_DFLLCTRL = 0;
    while (!(SYSCTRL_PCLKSR & (1u << 4))) {} /* wait DFLLRDY */
    SYSCTRL_DFLLCTRL = (1u << 1);            /* ENABLE */
    while (!(SYSCTRL_PCLKSR & (1u << 4))) {}

    /* Configure closed-loop: MUL=1465 (48MHz/32768), FSTEP=10, CSTEP=1 */
    SYSCTRL_DFLLMUL = (1465u) | (10u << 16) | (1u << 26);
    while (SYSCTRL_DFLLSYNC & (1u << 7)) {}

    /* Switch to closed-loop + WAITLOCK */
    SYSCTRL_DFLLCTRL = (1u << 1) | (1u << 2) | (1u << 11); /* ENABLE|MODE|WAITLOCK */
    while (!(SYSCTRL_PCLKSR & (1u << 4))) {}  /* DFLLRDY */
    while (!(SYSCTRL_PCLKSR & (1u << 6))) {}  /* DFLLLCKF */

    /* GCLK0 = DFLL48M (48 MHz system clock) */
    GCLK_GENDIV  = 0u;                                      /* GCLK0 divisor = 1 */
    while (GCLK_STATUS & (1u << 7)) {}
    GCLK_GENCTRL = (7u << 8) | (1u << 16);                 /* ID=0|SRC=DFLL48M|GCLKEN */
    while (GCLK_STATUS & (1u << 7)) {}
}

/* =========================================================================
 * Default handler and weak aliases
 * ========================================================================= */
void Default_Handler(void) { while (1) {} }

void NMI_Handler(void)      __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void)      __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void)  __attribute__((weak, alias("Default_Handler")));

/* SAMD21G18 peripheral IRQs (0-28) */
void PM_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void SYSCTRL_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void WDT_Handler(void)      __attribute__((weak, alias("Default_Handler")));
void RTC_Handler(void)      __attribute__((weak, alias("Default_Handler")));
void EIC_Handler(void)      __attribute__((weak, alias("Default_Handler")));
void NVMCTRL_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void DMAC_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void USB_Handler(void)      __attribute__((weak, alias("Default_Handler")));
void EVSYS_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void SERCOM0_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void SERCOM1_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void SERCOM2_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void SERCOM3_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void SERCOM4_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void SERCOM5_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void TCC0_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void TCC1_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void TCC2_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void TC3_Handler(void)      __attribute__((weak, alias("Default_Handler"))); /* KTOS tick */
void TC4_Handler(void)      __attribute__((weak, alias("Default_Handler")));
void TC5_Handler(void)      __attribute__((weak, alias("Default_Handler")));
void ADC_Handler(void)      __attribute__((weak, alias("Default_Handler")));
void AC_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void DAC_Handler(void)      __attribute__((weak, alias("Default_Handler")));
void PTC_Handler(void)      __attribute__((weak, alias("Default_Handler")));
void I2S_Handler(void)      __attribute__((weak, alias("Default_Handler")));

/* =========================================================================
 * Vector table at 0x00002000 (after 8 KB bootloader)
 * ========================================================================= */
typedef void (*isr_t)(void);

/* Reset_Handler forward declaration */
void Reset_Handler(void);

__attribute__((section(".vectors"), used))
static const isr_t vector_table[] = {
    (isr_t)&_estack,    /* Initial SP */
    Reset_Handler,       /* Reset */
    NMI_Handler,
    HardFault_Handler,
    0, 0, 0, 0, 0, 0, 0, /* Reserved */
    SVC_Handler,
    0, 0,               /* Reserved */
    PendSV_Handler,
    SysTick_Handler,
    /* External IRQs */
    PM_Handler,         /*  0 */
    SYSCTRL_Handler,    /*  1 */
    WDT_Handler,        /*  2 */
    RTC_Handler,        /*  3 */
    EIC_Handler,        /*  4 */
    NVMCTRL_Handler,    /*  5 */
    DMAC_Handler,       /*  6 */
    USB_Handler,        /*  7 */
    EVSYS_Handler,      /*  8 */
    SERCOM0_Handler,    /*  9 */
    SERCOM1_Handler,    /* 10 */
    SERCOM2_Handler,    /* 11 */
    SERCOM3_Handler,    /* 12 */
    SERCOM4_Handler,    /* 13 */
    SERCOM5_Handler,    /* 14 */
    TCC0_Handler,       /* 15 */
    TCC1_Handler,       /* 16 */
    TCC2_Handler,       /* 17 */
    TC3_Handler,        /* 18 — KTOS 1 ms tick */
    TC4_Handler,        /* 19 */
    TC5_Handler,        /* 20 */
    ADC_Handler,        /* 21 */
    AC_Handler,         /* 22 */
    DAC_Handler,        /* 23 */
    PTC_Handler,        /* 24 */
    I2S_Handler,        /* 25 */
};

/* =========================================================================
 * Reset handler
 * ========================================================================= */
void Reset_Handler(void)
{
    /* Relocate vector table to our application base */
    SCB_VTOR = 0x00002000UL;

    /* Bring system to 48 MHz */
    clock_init_48MHz();

    /* Copy .data from flash to RAM */
    const uint32_t *src = &_sidata;
    for (uint32_t *dst = &_sdata; (uintptr_t)dst < (uintptr_t)&_edata; ) { *dst++ = *src++; }

    /* Zero .bss */
    for (uint32_t *p = &_sbss; (uintptr_t)p < (uintptr_t)&_ebss; ) { *p++ = 0; }

    main();
    while (1) {}
}
