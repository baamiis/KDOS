/*
 * KTOS — Tiny Cooperative Task Switcher
 * Copyright (C) 2004-2025 Khalid Hamdou / BAAMIIS LIMITED
 * All rights reserved.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/**
 * @file startup.c
 * @brief STM32F103 (Blue Pill) bare-metal startup
 *
 * Provides:
 *   - Vector table (.vectors at 0x08000000)
 *   - Reset_Handler: 72 MHz PLL clock init, .data copy, .bss zero, main()
 *   - Default_Handler: infinite loop for unhandled exceptions
 *
 * Clock chain: HSE 8 MHz → PLL ×9 → SYSCLK 72 MHz
 *              AHB = 72 MHz, APB1 = 36 MHz, APB2 = 72 MHz
 *              ADC clock = APB2 / 6 = 12 MHz
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
 * RCC and FLASH registers used only during clock init
 * ========================================================================= */
#define FLASH_ACR  (*(volatile uint32_t *)0x40022000UL)
#define RCC_CR     (*(volatile uint32_t *)0x40021000UL)
#define RCC_CFGR   (*(volatile uint32_t *)0x40021004UL)

static void clock_init_72MHz(void)
{
    /* 2 NVM wait states required for 48–72 MHz */
    FLASH_ACR = (FLASH_ACR & ~0x7u) | 2u;

    /* Enable HSE (8 MHz external crystal) */
    RCC_CR |= (1u << 16);              /* HSEON */
    while (!(RCC_CR & (1u << 17))) {}  /* HSERDY */

    /* Configure PLL before enabling it:
       PLLSRC=HSE (bit 16), PLLMUL=×9 (bits[21:18]=0111),
       PPRE1=/2 (bits[10:8]=100) → APB1=36 MHz,
       ADCPRE=/6 (bits[15:14]=10) → ADC clock=12 MHz */
    RCC_CFGR = (1u << 16)   /* PLLSRC = HSE        */
             | (7u << 18)   /* PLLMUL = ×9          */
             | (4u << 8)    /* PPRE1  = /2 (APB1)   */
             | (2u << 14);  /* ADCPRE = /6           */

    /* Enable PLL, wait ready */
    RCC_CR |= (1u << 24);              /* PLLON  */
    while (!(RCC_CR & (1u << 25))) {}  /* PLLRDY */

    /* Switch system clock to PLL, wait for switch */
    RCC_CFGR |= 2u;                                        /* SW = PLL */
    while ((RCC_CFGR & (3u << 2)) != (2u << 2)) {}        /* SWS = PLL */
}

/* =========================================================================
 * Default handler and weak aliases
 * ========================================================================= */
void Default_Handler(void) { while (1) {} }

void NMI_Handler(void)              __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void)         __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void)       __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void)              __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void)         __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void)           __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void)          __attribute__((weak, alias("Default_Handler")));

void WWDG_IRQHandler(void)              __attribute__((weak, alias("Default_Handler")));
void PVD_IRQHandler(void)               __attribute__((weak, alias("Default_Handler")));
void TAMPER_IRQHandler(void)            __attribute__((weak, alias("Default_Handler")));
void RTC_IRQHandler(void)               __attribute__((weak, alias("Default_Handler")));
void FLASH_IRQHandler(void)             __attribute__((weak, alias("Default_Handler")));
void RCC_IRQHandler(void)               __attribute__((weak, alias("Default_Handler")));
void EXTI0_IRQHandler(void)             __attribute__((weak, alias("Default_Handler")));
void EXTI1_IRQHandler(void)             __attribute__((weak, alias("Default_Handler")));
void EXTI2_IRQHandler(void)             __attribute__((weak, alias("Default_Handler")));
void EXTI3_IRQHandler(void)             __attribute__((weak, alias("Default_Handler")));
void EXTI4_IRQHandler(void)             __attribute__((weak, alias("Default_Handler")));
void DMA1_Channel1_IRQHandler(void)     __attribute__((weak, alias("Default_Handler")));
void DMA1_Channel2_IRQHandler(void)     __attribute__((weak, alias("Default_Handler")));
void DMA1_Channel3_IRQHandler(void)     __attribute__((weak, alias("Default_Handler")));
void DMA1_Channel4_IRQHandler(void)     __attribute__((weak, alias("Default_Handler")));
void DMA1_Channel5_IRQHandler(void)     __attribute__((weak, alias("Default_Handler")));
void DMA1_Channel6_IRQHandler(void)     __attribute__((weak, alias("Default_Handler")));
void DMA1_Channel7_IRQHandler(void)     __attribute__((weak, alias("Default_Handler")));
void ADC1_2_IRQHandler(void)            __attribute__((weak, alias("Default_Handler")));
void USB_HP_CAN_TX_IRQHandler(void)     __attribute__((weak, alias("Default_Handler")));
void USB_LP_CAN_RX0_IRQHandler(void)    __attribute__((weak, alias("Default_Handler")));
void CAN_RX1_IRQHandler(void)           __attribute__((weak, alias("Default_Handler")));
void CAN_SCE_IRQHandler(void)           __attribute__((weak, alias("Default_Handler")));
void EXTI9_5_IRQHandler(void)           __attribute__((weak, alias("Default_Handler")));
void TIM1_BRK_IRQHandler(void)          __attribute__((weak, alias("Default_Handler")));
void TIM1_UP_IRQHandler(void)           __attribute__((weak, alias("Default_Handler")));
void TIM1_TRG_COM_IRQHandler(void)      __attribute__((weak, alias("Default_Handler")));
void TIM1_CC_IRQHandler(void)           __attribute__((weak, alias("Default_Handler")));
void TIM2_IRQHandler(void)              __attribute__((weak, alias("Default_Handler")));
void TIM3_IRQHandler(void)              __attribute__((weak, alias("Default_Handler")));
void TIM4_IRQHandler(void)              __attribute__((weak, alias("Default_Handler")));
void I2C1_EV_IRQHandler(void)           __attribute__((weak, alias("Default_Handler")));
void I2C1_ER_IRQHandler(void)           __attribute__((weak, alias("Default_Handler")));
void I2C2_EV_IRQHandler(void)           __attribute__((weak, alias("Default_Handler")));
void I2C2_ER_IRQHandler(void)           __attribute__((weak, alias("Default_Handler")));
void SPI1_IRQHandler(void)              __attribute__((weak, alias("Default_Handler")));
void SPI2_IRQHandler(void)              __attribute__((weak, alias("Default_Handler")));
void USART1_IRQHandler(void)            __attribute__((weak, alias("Default_Handler")));
void USART2_IRQHandler(void)            __attribute__((weak, alias("Default_Handler")));
void USART3_IRQHandler(void)            __attribute__((weak, alias("Default_Handler")));
void EXTI15_10_IRQHandler(void)         __attribute__((weak, alias("Default_Handler")));
void RTCAlarm_IRQHandler(void)          __attribute__((weak, alias("Default_Handler")));
void USBWakeup_IRQHandler(void)         __attribute__((weak, alias("Default_Handler")));

/* =========================================================================
 * Vector table at 0x08000000
 * ========================================================================= */
typedef void (*isr_t)(void);
void Reset_Handler(void);

__attribute__((section(".vectors"), used))
static const isr_t vector_table[] = {
    (isr_t)&_estack,           /* Initial SP                    */
    Reset_Handler,              /* Reset                         */
    NMI_Handler,
    HardFault_Handler,
    MemManage_Handler,
    BusFault_Handler,
    UsageFault_Handler,
    0, 0, 0, 0,                /* Reserved                      */
    SVC_Handler,
    DebugMon_Handler,
    0,                         /* Reserved                      */
    PendSV_Handler,
    SysTick_Handler,           /* KTOS 1 ms tick                */
    /* Peripheral IRQs 0–42 */
    WWDG_IRQHandler,           /*  0 */
    PVD_IRQHandler,            /*  1 */
    TAMPER_IRQHandler,         /*  2 */
    RTC_IRQHandler,            /*  3 */
    FLASH_IRQHandler,          /*  4 */
    RCC_IRQHandler,            /*  5 */
    EXTI0_IRQHandler,          /*  6 */
    EXTI1_IRQHandler,          /*  7 */
    EXTI2_IRQHandler,          /*  8 */
    EXTI3_IRQHandler,          /*  9 */
    EXTI4_IRQHandler,          /* 10 */
    DMA1_Channel1_IRQHandler,  /* 11 */
    DMA1_Channel2_IRQHandler,  /* 12 */
    DMA1_Channel3_IRQHandler,  /* 13 */
    DMA1_Channel4_IRQHandler,  /* 14 */
    DMA1_Channel5_IRQHandler,  /* 15 */
    DMA1_Channel6_IRQHandler,  /* 16 */
    DMA1_Channel7_IRQHandler,  /* 17 */
    ADC1_2_IRQHandler,         /* 18 */
    USB_HP_CAN_TX_IRQHandler,  /* 19 */
    USB_LP_CAN_RX0_IRQHandler, /* 20 */
    CAN_RX1_IRQHandler,        /* 21 */
    CAN_SCE_IRQHandler,        /* 22 */
    EXTI9_5_IRQHandler,        /* 23 */
    TIM1_BRK_IRQHandler,       /* 24 */
    TIM1_UP_IRQHandler,        /* 25 */
    TIM1_TRG_COM_IRQHandler,   /* 26 */
    TIM1_CC_IRQHandler,        /* 27 */
    TIM2_IRQHandler,           /* 28 */
    TIM3_IRQHandler,           /* 29 */
    TIM4_IRQHandler,           /* 30 */
    I2C1_EV_IRQHandler,        /* 31 */
    I2C1_ER_IRQHandler,        /* 32 */
    I2C2_EV_IRQHandler,        /* 33 */
    I2C2_ER_IRQHandler,        /* 34 */
    SPI1_IRQHandler,           /* 35 */
    SPI2_IRQHandler,           /* 36 */
    USART1_IRQHandler,         /* 37 */
    USART2_IRQHandler,         /* 38 */
    USART3_IRQHandler,         /* 39 */
    EXTI15_10_IRQHandler,      /* 40 */
    RTCAlarm_IRQHandler,       /* 41 */
    USBWakeup_IRQHandler,      /* 42 */
};

/* =========================================================================
 * Reset handler
 * ========================================================================= */
void Reset_Handler(void)
{
    clock_init_72MHz();

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
