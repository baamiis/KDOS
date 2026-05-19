/*
 * KTOS — STM32F030 Nucleo-F030R8 ADC example (bare-metal)
 *
 * Single KTOS task reads ADC1 channel 0 (PA0, CN7 pin 28) every 1 s.
 * Prints raw 12-bit count and millivolts over USART2.
 *
 * Reference: VDDA = 3.3 V → max input 3300 mV.
 *   mV = raw * 3300 / 4095
 *
 * ADC clock: synchronous PCLK/4 = 48/4 = 12 MHz (set via ADC_CFGR2.CKMODE).
 * USART2: PA2=TX, 115200 8N1 → ST-Link virtual COM.
 *
 * STM32F030 ADC uses new-style registers (ADC_CR, ADC_CFGR1/2, ADC_CHSELR).
 */

#include <stdint.h>
#include "../../../../core/ktos.h"

#define RCC_AHBENR   (*(volatile uint32_t *)0x40021014UL)
#define RCC_APB1ENR  (*(volatile uint32_t *)0x4002101CUL)
#define RCC_APB2ENR  (*(volatile uint32_t *)0x40021018UL)

#define GPIOA_MODER  (*(volatile uint32_t *)0x48000000UL)
#define GPIOA_AFRL   (*(volatile uint32_t *)0x48000020UL)

#define USART2_CR1   (*(volatile uint32_t *)0x40004400UL)
#define USART2_BRR   (*(volatile uint32_t *)0x4000440CUL)
#define USART2_ISR   (*(volatile uint32_t *)0x4000441CUL)
#define USART2_TDR   (*(volatile uint32_t *)0x40004428UL)
#define ISR_TXE  (1u << 7)

/* ADC1 new-style registers (STM32F030, base 0x40012400) */
#define ADC1_ISR     (*(volatile uint32_t *)0x40012400UL)
#define ADC1_CR      (*(volatile uint32_t *)0x40012408UL)
#define ADC1_CFGR1   (*(volatile uint32_t *)0x4001240CUL)
#define ADC1_CFGR2   (*(volatile uint32_t *)0x40012410UL)
#define ADC1_SMPR    (*(volatile uint32_t *)0x40012414UL)
#define ADC1_CHSELR  (*(volatile uint32_t *)0x40012428UL)
#define ADC1_DR      (*(volatile uint32_t *)0x40012440UL)

#define ADC_ADRDY   (1u << 0)
#define ADC_EOC     (1u << 2)
#define ADC_ADEN    (1u << 0)
#define ADC_ADSTART (1u << 2)
#define ADC_ADCAL   (1u << 31)

static void hw_init(void)
{
    RCC_AHBENR  |= (1u << 17);  /* GPIOA */
    RCC_APB1ENR |= (1u << 17);  /* USART2 */
    RCC_APB2ENR |= (1u << 9);   /* ADC1 */

    /* PA0=analog (MODER bits[1:0]=11) */
    GPIOA_MODER |= (3u << 0);

    /* PA2=TX AF1 */
    GPIOA_MODER = (GPIOA_MODER & ~(3u << 4)) | (2u << 4);
    GPIOA_AFRL  = (GPIOA_AFRL  & ~(0xFu << 8)) | (1u << 8);

    USART2_BRR = 417u;
    USART2_CR1 = (1u << 0) | (1u << 3);  /* UE | TE */

    /* ADC init: synchronous clock PCLK/4 (CKMODE=10 in CFGR2 bits[30:29]) */
    ADC1_CFGR2 = (2u << 29);   /* CKMODE = PCLK/4 = 12 MHz */

    /* Calibrate */
    ADC1_CR = ADC_ADCAL;
    while (ADC1_CR & ADC_ADCAL) {}

    /* Enable */
    ADC1_CR = ADC_ADEN;
    while (!(ADC1_ISR & ADC_ADRDY)) {}

    /* Configure: 12-bit, SW trigger, single, channel 0, 239.5 cycles */
    ADC1_CFGR1  = 0;            /* 12-bit, right-aligned, SW trigger */
    ADC1_CHSELR = (1u << 0);   /* select CH0 (PA0) */
    ADC1_SMPR   = 7u;           /* SMP = 111 → 239.5 cycles */
}

static uint16_t adc_read(void)
{
    ADC1_ISR = ADC_EOC;         /* clear flag */
    ADC1_CR |= ADC_ADSTART;
    while (!(ADC1_ISR & ADC_EOC)) {}
    return (uint16_t)(ADC1_DR & 0x0FFFu);
}

static void uart_putc(char c) { while (!(USART2_ISR & ISR_TXE)) {} USART2_TDR = (uint8_t)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void uart_putu32(uint32_t n)
{
    char buf[10]; uint8_t i = 0;
    if (!n) { uart_putc('0'); return; }
    while (n) { buf[i++] = (char)('0' + n % 10u); n /= 10u; }
    while (i--) uart_putc(buf[i]);
}

void ktos_timer_irq_handler(void);
void SysTick_Handler(void) { ktos_timer_irq_handler(); }
__attribute__((noreturn)) void ktos_Emergency(const char *msg)
{ uart_puts("\r\n[KTOS FATAL] "); uart_puts(msg); while (1) {} }
void ktos_DebugPrintf(const char *f, ...) { (void)f; }
void ktos_InitSys(void) {}

#define SAMPLE_MS 1000U

static WORD adc_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam; (void)lParam;
    if (MsgType == KTOS_MSG_TYPE_INIT) {
        uart_puts("=============================\r\n");
        uart_puts("  KTOS ADC — Nucleo-F030R8\r\n");
        uart_puts("  PA0 (A0), 12-bit, 3.3 V ref\r\n");
        uart_puts("=============================\r\n");
        return SAMPLE_MS;
    }
    uint16_t raw = adc_read();
    uint32_t mv  = ((uint32_t)raw * 3300UL) / 4095UL;
    uart_puts("A0: raw="); uart_putu32(raw);
    uart_puts("  "); uart_putu32(mv); uart_puts(" mV\r\n");
    return SAMPLE_MS;
}

int main(void)
{
    hw_init();
    ktos_InitTask(adc_task, 256, 4, 'A');
    ktos_RunOS();
    return 0;
}
