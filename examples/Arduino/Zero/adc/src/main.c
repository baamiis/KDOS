/*
 * KTOS — Arduino Zero ADC example (bare-metal)
 *
 * Single KTOS task reads A0 (PA02, AIN0) every 1 s.
 * Prints raw 12-bit value and millivolts.
 *
 * Reference: INTVCC1 = VCC/2 ≈ 1650 mV (VCC = 3.3 V).
 *   mv = raw * 1650 / 4095
 * Input range: 0–1.65 V.  Connect a signal ≤ 1.65 V to A0.
 * (For 0–3.3 V, add a 2:1 resistive divider to A0.)
 *
 * ADC clock: GCLK0 (48 MHz) / DIV32 = 1.5 MHz (≤ 2.1 MHz max).
 * Serial: PROGRAMMING port via EDBG → SERCOM5 (PA22=TX, PA23=RX).
 */

#include <stdint.h>
#include "../../../../../core/ktos.h"

/* PM / GCLK */
#define PM_APBCMASK  (*(volatile uint32_t *)0x40000420UL)
#define GCLK_CLKCTRL (*(volatile uint16_t *)0x40000C02UL)
#define GCLK_STATUS  (*(volatile uint8_t  *)0x40000C01UL)

/* PORT A */
#define PORTA_PMUX1    (*(volatile uint8_t  *)0x41004431UL) /* PA02/PA03 mux */
#define PORTA_PMUX11   (*(volatile uint8_t  *)0x4100443BUL)
#define PORTA_PINCFG2  (*(volatile uint8_t  *)0x41004442UL) /* PA02 */
#define PORTA_PINCFG22 (*(volatile uint8_t  *)0x41004456UL)
#define PORTA_PINCFG23 (*(volatile uint8_t  *)0x41004457UL)

/* SERCOM5 */
#define SERCOM5_CTRLA    (*(volatile uint32_t *)0x42001C00UL)
#define SERCOM5_CTRLB    (*(volatile uint32_t *)0x42001C04UL)
#define SERCOM5_BAUD     (*(volatile uint16_t *)0x42001C0CUL)
#define SERCOM5_INTFLAG  (*(volatile uint8_t  *)0x42001C18UL)
#define SERCOM5_SYNCBUSY (*(volatile uint32_t *)0x42001C1CUL)
#define SERCOM5_DATA     (*(volatile uint16_t *)0x42001C28UL)
#define DRE_FLAG (1u << 0)

static void sercom5_init(void)
{
    PM_APBCMASK |= (1u << 7);
    GCLK_CLKCTRL = (uint16_t)(25u | (0u << 8) | (1u << 14));
    while (GCLK_STATUS & (1u << 7)) {}
    PORTA_PMUX11   = (3u << 4) | 3u;
    PORTA_PINCFG22 = (1u << 0);
    PORTA_PINCFG23 = (1u << 0) | (1u << 1);
    SERCOM5_CTRLA = (1u << 0);
    while (SERCOM5_SYNCBUSY & (1u << 0)) {}
    SERCOM5_CTRLA = (0x4u) | (1u << 20) | (1u << 28);
    SERCOM5_CTRLB = (1u << 16) | (1u << 17);
    while (SERCOM5_SYNCBUSY & (1u << 2)) {}
    SERCOM5_BAUD = 63019u;
    SERCOM5_CTRLA |= (1u << 1);
    while (SERCOM5_SYNCBUSY & (1u << 1)) {}
}

static void uart_putc(char c) { while (!(SERCOM5_INTFLAG & DRE_FLAG)) {} SERCOM5_DATA = (uint8_t)c; }
static void uart_puts(const char *s) { while (*s) { uart_putc(*s++); } }

static void uart_putu32(uint32_t n)
{
    char buf[10]; uint8_t i = 0;
    if (!n) { uart_putc('0'); return; }
    while (n) { buf[i++] = (char)('0' + n % 10u); n /= 10u; }
    while (i--) { uart_putc(buf[i]); }
}

/* =========================================================================
 * ADC — AIN0 (PA02, A0), 12-bit, reference INTVCC1 (VCC/2), DIV32 clock
 * ADC GCLK peripheral ID = 30.  PM APBCMASK bit 16.
 * ========================================================================= */
#define ADC_CTRLA      (*(volatile uint8_t  *)0x42004000UL)
#define ADC_REFCTRL    (*(volatile uint8_t  *)0x42004001UL)
#define ADC_AVGCTRL    (*(volatile uint8_t  *)0x42004002UL)
#define ADC_SAMPCTRL   (*(volatile uint8_t  *)0x42004003UL)
#define ADC_CTRLB      (*(volatile uint16_t *)0x42004004UL)
#define ADC_SWTRIG     (*(volatile uint8_t  *)0x4200400CUL)
#define ADC_INPUTCTRL  (*(volatile uint32_t *)0x42004010UL)
#define ADC_INTFLAG    (*(volatile uint8_t  *)0x42004018UL)
#define ADC_SYNCBUSY   (*(volatile uint8_t  *)0x42004019UL)
#define ADC_RESULT     (*(volatile uint16_t *)0x4200401AUL)

static void adc_init(void)
{
    /* Enable ADC APB clock (APBCMASK bit 16) */
    PM_APBCMASK |= (1u << 16);

    /* Connect GCLK0 to ADC (GCLK peripheral ID 30) */
    GCLK_CLKCTRL = (uint16_t)(30u | (0u << 8) | (1u << 14));
    while (GCLK_STATUS & (1u << 7)) {}

    /* PA02 as AIN0 (peripheral B = function 1) */
    PORTA_PMUX1   = 1u;          /* PA02 even nibble = B */
    PORTA_PINCFG2 = (1u << 0);  /* PMUXEN, no digital input */

    /* Reset ADC */
    ADC_CTRLA = (1u << 0);  /* SWRST */
    while (ADC_SYNCBUSY & (1u << 7)) {}
    while (ADC_CTRLA & (1u << 0)) {}

    /* Reference: INTVCC1 = VCC/2 ≈ 1.65 V */
    ADC_REFCTRL  = 0x02u;   /* REFSEL = INTVCC1 */
    /* 12-bit, single-ended, clock prescaler DIV32 (PRESCALER=3) */
    ADC_CTRLB    = (3u << 8);  /* PRESCALER=3 → DIV32, RESSEL=0→12-bit */
    /* Input: MUXPOS=AIN0(0), MUXNEG=GND(0x18), GAIN=1x(0) */
    ADC_INPUTCTRL = (0u) | (0x18u << 8);

    /* Enable ADC */
    ADC_CTRLA = (1u << 1);  /* ENABLE */
    while (ADC_SYNCBUSY & (1u << 7)) {}

    /* Discard first conversion (required after enable) */
    ADC_SWTRIG = (1u << 1);
    while (!(ADC_INTFLAG & (1u << 0))) {}
    ADC_INTFLAG = (1u << 0);  /* clear RESRDY */
}

static uint16_t adc_read(void)
{
    ADC_SWTRIG = (1u << 1);  /* START */
    while (!(ADC_INTFLAG & (1u << 0))) {}  /* wait RESRDY */
    ADC_INTFLAG = (1u << 0);
    return ADC_RESULT & 0x0FFFu;
}

/* TC3 tick */
void TC3_Handler(void) { *(volatile uint8_t *)0x42002C0EUL = 1u; ktos_timer_irq_handler(); }

__attribute__((noreturn))
void ktos_Emergency(const char *msg) { uart_puts("\r\n[KTOS FATAL] "); uart_puts(msg); while (1) {} }
void ktos_DebugPrintf(const char *f, ...) { (void)f; }
void ktos_InitSys(void) {}

#define SAMPLE_MS 1000U

static WORD adc_task(WORD MsgType, WORD Param1, LONG Param2)
{
    (void)Param1; (void)Param2;
    if (MsgType == KTOS_MSG_TYPE_INIT) {
        adc_init();
        uart_puts("=============================\r\n");
        uart_puts("  KTOS ADC Example\r\n");
        uart_puts("  Arduino Zero (SAMD21G18)\r\n");
        uart_puts("  A0 = PA02 = AIN0\r\n");
        uart_puts("  Ref: INTVCC1 = VCC/2 = 1.65 V\r\n");
        uart_puts("=============================\r\n");
        return SAMPLE_MS;
    }
    uint16_t raw = adc_read();
    /* mv = raw * 1650 / 4095 */
    uint32_t mv = ((uint32_t)raw * 1650UL) / 4095UL;
    uart_puts("A0: raw=");
    uart_putu32(raw);
    uart_puts("  ");
    uart_putu32(mv);
    uart_puts(" mV\r\n");
    return SAMPLE_MS;
}

int main(void)
{
    sercom5_init();
    ktos_InitTask(adc_task, 128, 4, 'A');
    ktos_RunOS();
    return 0;
}
