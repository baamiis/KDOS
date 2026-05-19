/*
 * KTOS — Arduino Due ADC example (bare-metal)
 *
 * Single KTOS task reads A0 (PA16, ADC channel 7) every 1 s.
 * Prints raw 12-bit value and millivolts (3.3 V reference).
 *
 * SAM3X8E ADC: 12-bit, max clock 1 MHz.
 * A0 = PA16 = AD7 (channel 7).
 * Reference voltage: 3.3 V (ADVREF pin on Due).
 *
 * Serial: connect via the PROGRAMMING port (small USB near reset).
 */

#include <stdint.h>
#include "../../../../../core/ktos.h"

/* PMC */
#define PMC_PCER0  (*(volatile uint32_t *)0x400E0610UL)
#define PMC_PCER1  (*(volatile uint32_t *)0x400E0700UL)

/* PIOA — UART0 */
#define PIOA_PDR   (*(volatile uint32_t *)0x400E0E04UL)
#define PIOA_ABSR  (*(volatile uint32_t *)0x400E0E70UL)

/* UART0 */
#define UART0_CR   (*(volatile uint32_t *)0x400E0800UL)
#define UART0_MR   (*(volatile uint32_t *)0x400E0804UL)
#define UART0_SR   (*(volatile uint32_t *)0x400E0814UL)
#define UART0_THR  (*(volatile uint32_t *)0x400E081CUL)
#define UART0_BRGR (*(volatile uint32_t *)0x400E0820UL)
#define UART_SR_TXRDY (1u << 1)

static void uart_init(void)
{
    PMC_PCER0  = (1u << 11) | (1u << 8);
    PIOA_PDR   = (1u << 8) | (1u << 9);
    PIOA_ABSR &= ~((1u << 8) | (1u << 9));
    UART0_CR   = (1u << 2) | (1u << 3);
    UART0_MR   = (4u << 9);
    UART0_BRGR = 46u;
    UART0_CR   = (1u << 4) | (1u << 6);
}

static void uart_putc(char c) { while (!(UART0_SR & UART_SR_TXRDY)) {} UART0_THR = (uint8_t)c; }
static void uart_puts(const char *s) { while (*s) { uart_putc(*s++); } }

static void uart_putu32(uint32_t n)
{
    char buf[10]; uint8_t i = 0;
    if (!n) { uart_putc('0'); return; }
    while (n) { buf[i++] = (char)('0' + n % 10); n /= 10; }
    while (i--) { uart_putc(buf[i]); }
}

/* =========================================================================
 * ADC — channel 7 (A0 = PA16), software trigger, 12-bit, 1 MHz clock
 * ADC_CLK = MCK / (2*(PRESCAL+1)) → PRESCAL=41 gives 84/(2*42)=1 MHz
 * ========================================================================= */
#define ADC_CR    (*(volatile uint32_t *)0x400C0000UL)
#define ADC_MR    (*(volatile uint32_t *)0x400C0004UL)
#define ADC_CHER  (*(volatile uint32_t *)0x400C0010UL)
#define ADC_SR    (*(volatile uint32_t *)0x400C0030UL)
#define ADC_LCDR  (*(volatile uint32_t *)0x400C0020UL)
#define ADC_DRDY  (1u << 24)
#define ADC_PMC_ID 37u

static void adc_init(void)
{
    PMC_PCER1 = (1u << (ADC_PMC_ID - 32));   /* enable ADC clock (ID 37) */
    ADC_CR    = (1u << 0);                    /* SWRST */
    ADC_MR    = (41u << 8);                   /* PRESCAL=41, software trigger, 12-bit */
    ADC_CHER  = (1u << 7);                    /* enable channel 7 (A0/PA16) */
}

static uint16_t adc_read(void)
{
    ADC_CR = (1u << 1);                        /* START */
    while (!(ADC_SR & ADC_DRDY)) {}
    return (uint16_t)(ADC_LCDR & 0x0FFFu);    /* 12-bit result */
}

/* TC0 — KTOS tick */
#define TC0_CH0_SR (*(volatile uint32_t *)0x40080020UL)
void TC0_Handler(void) { (void)TC0_CH0_SR; ktos_timer_irq_handler(); }

__attribute__((noreturn))
void ktos_Emergency(const char *msg) { uart_puts("\r\n[KTOS FATAL] "); uart_puts(msg); while (1) {} }
void ktos_DebugPrintf(const char *f, ...) { (void)f; }
void ktos_InitSys(void) {}

#define SAMPLE_MS 1000U

static WORD adc_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam; (void)lParam;
    if (MsgType == KTOS_MSG_TYPE_INIT) {
        adc_init();
        uart_puts("=============================\r\n");
        uart_puts("  KTOS ADC Example\r\n");
        uart_puts("  Arduino Due (SAM3X8E)\r\n");
        uart_puts("  A0 = PA16 = ADC channel 7\r\n");
        uart_puts("  12-bit, 3.3 V reference\r\n");
        uart_puts("=============================\r\n");
        return SAMPLE_MS;
    }

    uint16_t raw = adc_read();
    /* mv = raw * 3300 / 4095 */
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
    uart_init();
    ktos_InitTask(adc_task, 128, 4, 'A');
    ktos_RunOS();
    return 0;
}
