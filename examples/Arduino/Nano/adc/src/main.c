/*
 * KTOS — Arduino Nano ADC example (bare-metal)
 *
 * A single KTOS task drives the whole program:
 *
 *   KTOS_MSG_TYPE_INIT   -> print banner
 *   KTOS_MSG_TYPE_TIMER  -> read ADC channel 0, print raw + millivolts,
 *                           sleep 1000 ms
 *
 * No Arduino framework, no Serial, no analogRead — just AVR registers
 * and KTOS.  Voltage is reported in millivolts to avoid pulling in
 * the floating-point printf at link time.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>

#include "../../../../../core/ktos.h"

/* =========================================================================
 * USART0 — direct register access, 115200 8N1 at 16 MHz
 *
 * At F_CPU = 16 MHz the canonical setting for 115200 baud is
 *   U2X0 = 1, UBRR0 = 16  -> actual 117647 baud (2.1% error — fine).
 * ========================================================================= */

static void uart_init(void)
{
    UBRR0H = 0;
    UBRR0L = 16;
    UCSR0A = (1 << U2X0);                       /* 2× baud rate */
    UCSR0B = (1 << TXEN0) | (1 << RXEN0);       /* TX + RX enabled */
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);     /* 8N1 */
}

static void uart_putc(char c)
{
    while (!(UCSR0A & (1 << UDRE0))) { }
    UDR0 = (uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s) { uart_putc(*s++); }
}

static void uart_putu16(uint16_t n)
{
    char buf[6];
    uint8_t i = 0;
    if (n == 0) { uart_putc('0'); return; }
    while (n) { buf[i++] = (char)('0' + (n % 10)); n /= 10; }
    while (i--) { uart_putc(buf[i]); }
}

/* Print millivolts (0..5000) as "X.XX V". */
static void uart_put_mv_as_volts(uint16_t mv)
{
    uart_putu16(mv / 1000);
    uart_putc('.');
    uint16_t frac = mv % 1000;             /* 0..999 */
    uint16_t hundredths = frac / 10;       /* 0..99  */
    if (hundredths < 10) { uart_putc('0'); }
    uart_putu16(hundredths);
    uart_putc('V');
}

/* =========================================================================
 * ADC — channel 0 (A0 / PC0), AVcc reference, prescaler 128 → 125 kHz
 * ========================================================================= */

static void adc_init(void)
{
    /* REFS0 = 1, REFS1 = 0  -> AVcc with external cap at AREF */
    ADMUX  = (1 << REFS0);
    /* Enable, prescaler 128 (16 MHz / 128 = 125 kHz — in spec range). */
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
}

static uint16_t adc_read(uint8_t channel)
{
    ADMUX = (ADMUX & ~0x0F) | (channel & 0x0F);
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC)) { }
    return ADC;
}

/* =========================================================================
 * Required KTOS platform callbacks
 * ========================================================================= */

__attribute__((noreturn))
void ktos_Emergency(const char *msg)
{
    uart_puts("\r\n[KTOS FATAL] ");
    uart_puts(msg);
    uart_puts("\r\n");
    while (1) { }
}

void ktos_DebugPrintf(const char *fmt, ...) { (void)fmt; }
void ktos_InitSys(void) { }

/* =========================================================================
 * KTOS 1 ms tick — Timer1 CTC, set up by the AVR BSP
 * ========================================================================= */

extern void ktos_timer_irq_handler(void);

ISR(TIMER1_COMPA_vect)
{
    ktos_timer_irq_handler();
}

/* =========================================================================
 * ADC task
 * ========================================================================= */

#define ADC_CHANNEL_A0  0
#define SAMPLE_PERIOD_MS 1000U

static WORD adc_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam;
    (void)lParam;

    if (MsgType == KTOS_MSG_TYPE_INIT) {
        uart_puts("=============================\r\n");
        uart_puts("  KTOS ADC Example\r\n");
        uart_puts("=============================\r\n");
        uart_puts("Reading A0 every 1 s...\r\n");
    }

    /* KDOS ADC HAL -> ktos_hal_adc_read(ADC_CHANNEL_A0). */
    uint16_t raw = adc_read(ADC_CHANNEL_A0);

    /* mv = raw * 5000 / 1023.  Multiply first in 32-bit to avoid overflow. */
    uint16_t mv = (uint16_t)(((uint32_t)raw * 5000UL) / 1023UL);

    uart_puts("A0 raw=");
    uart_putu16(raw);
    uart_puts(" voltage=");
    uart_put_mv_as_volts(mv);
    uart_puts("\r\n");

    return SAMPLE_PERIOD_MS;     /* wake again with KTOS_MSG_TYPE_TIMER */
}

/* =========================================================================
 * Entry point — runs on the avr-libc startup stack until ktos_RunOS().
 * ========================================================================= */

int main(void)
{
    uart_init();
    adc_init();

    /* 96 stack words = 384 bytes — comfortable for our printing chain. */
    ktos_InitTask(adc_task,
                  /* StackSize  = */ 96,
                  /* QueueSize  = */ 4,
                  /* TaskID     = */ 'A');

    ktos_RunOS();                /* never returns */
    return 0;                    /* unreachable */
}
