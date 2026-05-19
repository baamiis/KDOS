/*
 * KTOS — Arduino Mega 2560 ADC example (bare-metal)
 *
 * A single KTOS task drives the whole program:
 *
 *   KTOS_MSG_TYPE_INIT   -> print banner, configure ADC
 *   KTOS_MSG_TYPE_TIMER  -> read ADC channel 0 (A0 / PF0), print raw
 *                           value and millivolts, sleep 1000 ms
 *
 * No Arduino framework, no analogRead — just AVR registers and KTOS.
 * Voltage is reported in millivolts to avoid floating-point printf.
 *
 * A0 on the Mega 2560 = PF0 (same ADC channel 0 as on the Nano/Uno).
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>

#include "../../../../../core/ktos.h"

/* =========================================================================
 * USART0 — 115200 8N1 (U2X0=1, UBRR0=16)
 * ========================================================================= */

static void uart_init(void)
{
    UBRR0H = 0;
    UBRR0L = 16;
    UCSR0A = (1 << U2X0);
    UCSR0B = (1 << TXEN0) | (1 << RXEN0);
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
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

static void uart_putu32(uint32_t n)
{
    char buf[10];
    uint8_t i = 0;
    if (n == 0) { uart_putc('0'); return; }
    while (n) { buf[i++] = (char)('0' + n % 10); n /= 10; }
    while (i--) { uart_putc(buf[i]); }
}

/* =========================================================================
 * ADC — channel 0 (A0 / PF0), AVCC reference, 10-bit
 * ========================================================================= */

static void adc_init(void)
{
    ADMUX  = (1 << REFS0);                               /* AVCC ref, ch 0 */
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
                                                          /* enable, prescaler 128 */
}

static uint16_t adc_read(void)
{
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC)) { }
    return ADC;
}

/* =========================================================================
 * KTOS platform callbacks
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

ISR(TIMER1_COMPA_vect) { ktos_timer_irq_handler(); }

/* =========================================================================
 * ADC task
 * ========================================================================= */

static WORD adc_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam;
    (void)lParam;

    if (MsgType == KTOS_MSG_TYPE_INIT) {
        adc_init();
        uart_puts("=============================\r\n");
        uart_puts("  KTOS ADC Example\r\n");
        uart_puts("  Arduino Mega 2560\r\n");
        uart_puts("=============================\r\n");
        uart_puts("Reading A0 (PF0) every 1 s\r\n");
        return 1000;
    }

    uint16_t raw = adc_read();
    uint32_t mv  = ((uint32_t)raw * 5000UL) / 1023UL;

    uart_puts("A0: raw=");
    uart_putu32(raw);
    uart_puts("  ");
    uart_putu32(mv);
    uart_puts(" mV\r\n");

    return 1000;
}

/* =========================================================================
 * Entry point
 * ========================================================================= */

int main(void)
{
    uart_init();
    ktos_InitTask(adc_task, 96, 4, 'A');
    ktos_RunOS();
    return 0;
}
