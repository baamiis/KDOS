/*
 * KTOS — Tiny Cooperative Task Switcher
 * Copyright (C) 2004-2025 Khalid Hamdou / BAAMIIS LIMITED
 * All rights reserved.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/**
 * @file startup.c
 * @brief STM32F030R8 (Nucleo) bare-metal startup
 *
 * Provides:
 *   - Vector table (.vectors at 0x08000000)
 *   - Reset_Handler: 48 MHz PLL clock init, .data copy, .bss zero, main()
 *   - Default_Handler: infinite loop for unhandled exceptions
 *
 * Clock chain: HSI 8 MHz → HSI/2 (4 MHz) → PLL ×12 → SYSCLK 48 MHz
 *              AHB = 48 MHz, APB = 48 MHz
 *
 * Every main.c must define:
 *   void SysTick_Handler(void) { ktos_timer_irq_handler(); }
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
 * RCC and FLASH registers used during clock init
 * ========================================================================= */
#define FLASH_ACR  (*(volatile uint32_t *)0x40022000UL)
#define RCC_CR     (*(volatile uint32_t *)0x40021000UL)
#define RCC_CFGR   (*(volatile uint32_t *)0x40021004UL)

static void clock_init_48MHz(void)
{
    /* 1 NVM wait state required for 24–48 MHz; enable prefetch */
    FLASH_ACR = (1u << 4) | 1u;   /* PRFTBE | LATENCY=1 */

    /* PLL: source = HSI/2 (PLLSRC=0, default), PLLMUL = ×12 (bits[21:18]=1010) */
    RCC_CFGR = (10u << 18);        /* PLLMUL = ×12 → 4 MHz × 12 = 48 MHz */

    /* Enable PLL, wait ready */
    RCC_CR |= (1u << 24);              /* PLLON  */
    while (!(RCC_CR & (1u << 25))) {}  /* PLLRDY */

    /* Switch system clock to PLL, wait for switch */
    RCC_CFGR |= 2u;                                    /* SW = PLL */
    while ((RCC_CFGR & (3u << 2)) != (2u << 2)) {}    /* SWS = PLL */
}

/* =========================================================================
 * Default handler and weak aliases
 * STM32F030 peripheral IRQs 0–31 (RM0360 Table 36)
 * ========================================================================= */
void Default_Handler(void) { while (1) {} }

void NMI_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void)      __attribute__((weak, alias("Default_Handler")));

void WWDG_IRQHandler(void)          __attribute__((weak, alias("Default_Handler")));
void RTC_IRQHandler(void)           __attribute__((weak, alias("Default_Handler")));
void FLASH_IRQHandler(void)         __attribute__((weak, alias("Default_Handler")));
void RCC_IRQHandler(void)           __attribute__((weak, alias("Default_Handler")));
void EXTI0_1_IRQHandler(void)       __attribute__((weak, alias("Default_Handler")));
void EXTI2_3_IRQHandler(void)       __attribute__((weak, alias("Default_Handler")));
void EXTI4_15_IRQHandler(void)      __attribute__((weak, alias("Default_Handler")));
void DMA1_Channel1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Channel2_3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Channel4_5_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ADC1_IRQHandler(void)          __attribute__((weak, alias("Default_Handler")));
void TIM1_BRK_UP_TRG_COM_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM1_CC_IRQHandler(void)       __attribute__((weak, alias("Default_Handler")));
void TIM3_IRQHandler(void)          __attribute__((weak, alias("Default_Handler")));
void TIM6_IRQHandler(void)          __attribute__((weak, alias("Default_Handler")));
void TIM14_IRQHandler(void)         __attribute__((weak, alias("Default_Handler")));
void TIM15_IRQHandler(void)         __attribute__((weak, alias("Default_Handler")));
void TIM16_IRQHandler(void)         __attribute__((weak, alias("Default_Handler")));
void TIM17_IRQHandler(void)         __attribute__((weak, alias("Default_Handler")));
void I2C1_IRQHandler(void)          __attribute__((weak, alias("Default_Handler")));
void I2C2_IRQHandler(void)          __attribute__((weak, alias("Default_Handler")));
void SPI1_IRQHandler(void)          __attribute__((weak, alias("Default_Handler")));
void SPI2_IRQHandler(void)          __attribute__((weak, alias("Default_Handler")));
void USART1_IRQHandler(void)        __attribute__((weak, alias("Default_Handler")));
void USART2_IRQHandler(void)        __attribute__((weak, alias("Default_Handler")));
void USART3_4_IRQHandler(void)      __attribute__((weak, alias("Default_Handler")));
void USB_IRQHandler(void)           __attribute__((weak, alias("Default_Handler")));

/* =========================================================================
 * Vector table at 0x08000000
 * ========================================================================= */
typedef void (*isr_t)(void);
void Reset_Handler(void);

__attribute__((section(".vectors"), used))
static const isr_t vector_table[] = {
    (isr_t)&_estack,           /* Initial SP                     */
    Reset_Handler,              /* Reset                          */
    NMI_Handler,
    HardFault_Handler,
    0, 0, 0, 0, 0, 0, 0,      /* Reserved (M0 has no M3 faults) */
    SVC_Handler,
    0, 0,                      /* Reserved                       */
    PendSV_Handler,
    SysTick_Handler,           /* KTOS 1 ms tick                 */
    /* Peripheral IRQs 0–31 (RM0360 Table 36) */
    WWDG_IRQHandler,                    /*  0 */
    RTC_IRQHandler,                     /*  1 */
    FLASH_IRQHandler,                   /*  2 */
    RCC_IRQHandler,                     /*  3 */
    EXTI0_1_IRQHandler,                 /*  4 */
    EXTI2_3_IRQHandler,                 /*  5 */
    EXTI4_15_IRQHandler,                /*  6 */
    0,                                  /*  7 Reserved */
    DMA1_Channel1_IRQHandler,           /*  8 */
    DMA1_Channel2_3_IRQHandler,         /*  9 */
    DMA1_Channel4_5_IRQHandler,         /* 10 */
    ADC1_IRQHandler,                    /* 11 */
    TIM1_BRK_UP_TRG_COM_IRQHandler,     /* 12 */
    TIM1_CC_IRQHandler,                 /* 13 */
    0,                                  /* 14 Reserved */
    TIM3_IRQHandler,                    /* 15 */
    TIM6_IRQHandler,                    /* 16 */
    0,                                  /* 17 Reserved */
    TIM14_IRQHandler,                   /* 18 */
    TIM15_IRQHandler,                   /* 19 */
    TIM16_IRQHandler,                   /* 20 */
    TIM17_IRQHandler,                   /* 21 */
    I2C1_IRQHandler,                    /* 22 */
    I2C2_IRQHandler,                    /* 23 */
    SPI1_IRQHandler,                    /* 24 */
    SPI2_IRQHandler,                    /* 25 */
    USART1_IRQHandler,                  /* 26 */
    USART2_IRQHandler,                  /* 27 */
    USART3_4_IRQHandler,                /* 28 */
    0,                                  /* 29 Reserved */
    0,                                  /* 30 Reserved */
    USB_IRQHandler,                     /* 31 */
};

/* =========================================================================
 * Reset handler
 * ========================================================================= */
void Reset_Handler(void)
{
    clock_init_48MHz();

    /* Copy .data from flash to RAM */
    const uint32_t *src = &_sidata;
    for (uint32_t *dst = &_sdata; (uintptr_t)dst < (uintptr_t)&_edata; ) {
        *dst++ = *src++;
    }

    /* Zero .bss */
    for (uint32_t *p = &_sbss; (uintptr_t)p < (uintptr_t)&_ebss; ) {
        *p++ = 0;
    }

    main();
    while (1) {}
}

/* Minimal newlib stub — heap unused; prevents _sbrk link error */
void *_sbrk(int incr) { (void)incr; return (void *)-1; }
