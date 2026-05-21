/*
 * KTOS — STM32F103 Blue Pill ADC example (bare-metal)
 *
 * Single KTOS task reads ADC1 channel 0 (PA0, pin A0) every 1 s.
 * Prints raw 12-bit count and millivolts over USART1.
 *
 * Reference: VDDA = 3.3 V → max input = 3300 mV.
 *   mV = raw * 3300 / 4095
 *
 * ADC clock: APB2 (72 MHz) / 6 = 12 MHz (≤ 14 MHz max for STM32F103).
 * Prescaler is set in startup.c clock_init (RCC_CFGR ADCPRE = /6).
 *
 * USART1: PA9=TX, 115200 8N1, 72 MHz APB2.
 * Wiring: connect a 0–3.3 V signal to PA0 (A0).
 */

#include <stdint.h>
#include "../../../../core/ktos.h"

/* =========================================================================
 * RCC / GPIO / USART1 / ADC1
 * ========================================================================= */
#define RCC_APB2ENR  (*(volatile uint32_t *)0x40021018UL)

#define GPIOA_CRL    (*(volatile uint32_t *)0x40010800UL)
#define GPIOA_CRH    (*(volatile uint32_t *)0x40010804UL)

#define USART1_SR    (*(volatile uint32_t *)0x40013800UL)
#define USART1_DR    (*(volatile uint32_t *)0x40013804UL)
#define USART1_BRR   (*(volatile uint32_t *)0x40013808UL)
#define USART1_CR1   (*(volatile uint32_t *)0x4001380CUL)
#define SR_TXE  (1u << 7)

#define ADC1_SR      (*(volatile uint32_t *)0x40012400UL)
#define ADC1_CR1     (*(volatile uint32_t *)0x40012404UL)
#define ADC1_CR2     (*(volatile uint32_t *)0x40012408UL)
#define ADC1_SMPR2   (*(volatile uint32_t *)0x40012410UL)
#define ADC1_SQR1    (*(volatile uint32_t *)0x4001242CUL)
#define ADC1_SQR3    (*(volatile uint32_t *)0x40012434UL)
#define ADC1_DR      (*(volatile uint32_t *)0x4001244CUL)

#define ADC_EOC      (1u << 1)
#define ADC_ADON     (1u << 0)
#define ADC_RSTCAL   (1u << 3)
#define ADC_CAL      (1u << 2)
#define ADC_SWSTART  (1u << 22)
#define ADC_EXTTRIG  (1u << 20)

static void hw_init(void)
{
    RCC_APB2ENR |= (1u << 2) | (1u << 9) | (1u << 14);  /* GPIOA + ADC1 + USART1 */

    /* PA0: analog input (MODE=00, CNF=00 → 0x0) */
    GPIOA_CRL &= ~0xFu;

    /* PA9=TX AF PP 50 MHz */
    GPIOA_CRH = (GPIOA_CRH & ~(0xFu << 4)) | (0xBu << 4);

    USART1_BRR = 625u;
    USART1_CR1 = (1u << 13) | (1u << 3);  /* UE | TE */

    /* ADC1 init: CH0, SW trigger, 71.5-cycle sample time */
    ADC1_CR2  = 0;
    ADC1_SQR1 = 0;  /* sequence length = 1 */
    ADC1_SQR3 = 0;  /* SQ1 = channel 0 */
    ADC1_SMPR2 = 7u; /* SMP0 = 111 → 71.5 cycles */
    /* EXTSEL=111 (SW trigger), EXTTRIG=1, ADON=1 */
    ADC1_CR2 = (7u << 17) | ADC_EXTTRIG | ADC_ADON;

    /* tSTAB: wait ~1 µs */
    for (volatile uint32_t i = 0; i < 100u; ++i) {}

    /* Calibration */
    ADC1_CR2 |= ADC_RSTCAL;
    while (ADC1_CR2 & ADC_RSTCAL) {}
    ADC1_CR2 |= ADC_CAL;
    while (ADC1_CR2 & ADC_CAL) {}
}

static uint16_t adc_read(void)
{
    ADC1_CR2 |= ADC_SWSTART;
    while (!(ADC1_SR & ADC_EOC)) {}
    return (uint16_t)(ADC1_DR & 0x0FFFu);
}

static void uart_putc(char c) { while (!(USART1_SR & SR_TXE)) {} USART1_DR = (uint8_t)c; }
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

static WORD adc_task(WORD MsgType, WORD Param1, LONG Param2)
{
    (void)Param1; (void)Param2;
    if (MsgType == KTOS_MSG_TYPE_INIT) {
        uart_puts("=============================\r\n");
        uart_puts("  KTOS ADC — Blue Pill\r\n");
        uart_puts("  PA0 (A0), 12-bit, 3.3 V ref\r\n");
        uart_puts("=============================\r\n");
        return SAMPLE_MS;
    }
    uint16_t raw = adc_read();
    uint32_t mv  = ((uint32_t)raw * 3300UL) / 4095UL;
    uart_puts("A0: raw=");
    uart_putu32(raw);
    uart_puts("  ");
    uart_putu32(mv);
    uart_puts(" mV\r\n");
    return SAMPLE_MS;
}

int main(void)
{
    hw_init();
    ktos_InitTask(adc_task, 256, 4, 'A');
    ktos_RunOS();
    return 0;
}
