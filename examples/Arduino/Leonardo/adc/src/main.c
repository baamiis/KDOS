/*
 * KTOS — Arduino Leonardo ADC example (bare-metal)
 *
 * A single KTOS task drives the whole program:
 *
 *   KTOS_MSG_TYPE_INIT   -> print banner, configure ADC
 *   KTOS_MSG_TYPE_TIMER  -> read A0, print raw value and millivolts,
 *                           sleep 1000 ms
 *
 * No Arduino framework, no analogRead — just AVR registers and KTOS.
 *
 * *** ATmega32U4 ADC channel note ***
 * On the Leonardo, A0 is PF7, which maps to ADC channel 7 — not channel
 * 0 as on the UNO/Nano (PC0).  ADMUX[MUX4:0] must be set to 0b00111 (7).
 *
 * Serial output uses USART1 on D0(RX)/D1(TX).  Connect a USB-to-serial
 * adapter — adapter-RX → D1, adapter-TX → D0.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>

#include "../../../../../core/ktos.h"

/* =========================================================================
 * USART1 — 115200 8N1 (U2X1=1, UBRR1=16)
 * ========================================================================= */

static void uart_init(void)
{
    UBRR1H = 0;
    UBRR1L = 16;
    UCSR1A = (1 << U2X1);
    UCSR1B = (1 << TXEN1) | (1 << RXEN1);
    UCSR1C = (1 << UCSZ11) | (1 << UCSZ10);
}

static void uart_putc(char c)
{
    while (!(UCSR1A & (1 << UDRE1))) { }
    UDR1 = (uint8_t)c;
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

static void uart_put_mv_as_volts(uint16_t mv)
{
    uart_putu16(mv / 1000);
    uart_putc('.');
    uint16_t frac = mv % 1000;
    uint16_t hundredths = frac / 10;
    if (hundredths < 10) { uart_putc('0'); }
    uart_putu16(hundredths);
    uart_putc('V');
}

/* =========================================================================
 * ADC — A0 = PF7 = ADC channel 7 on ATmega32U4, AVCC reference
 * ========================================================================= */

#define ADC_CHANNEL_A0  7   /* PF7 = ADC7 on ATmega32U4 */

static void adc_init(void)
{
    ADMUX  = (1 << REFS0);                                /* AVCC ref, ch 0 temp */
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
                                                           /* enable, prescaler 128 */
}

static uint16_t adc_read(uint8_t channel)
{
    ADMUX = (ADMUX & 0xE0) | (channel & 0x1F);
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

#define SAMPLE_PERIOD_MS 1000U

static WORD adc_task(WORD MsgType, WORD Param1, LONG Param2)
{
    (void)Param1;
    (void)Param2;

    if (MsgType == KTOS_MSG_TYPE_INIT) {
        adc_init();
        uart_puts("=============================\r\n");
        uart_puts("  KTOS ADC Example\r\n");
        uart_puts("  Arduino Leonardo\r\n");
        uart_puts("  A0 = PF7 = ADC channel 7\r\n");
        uart_puts("=============================\r\n");
        uart_puts("Reading A0 every 1 s\r\n");
        return SAMPLE_PERIOD_MS;
    }

    uint16_t raw = adc_read(ADC_CHANNEL_A0);
    uint16_t mv  = (uint16_t)(((uint32_t)raw * 5000UL) / 1023UL);

    uart_puts("A0: raw=");
    uart_putu16(raw);
    uart_puts("  voltage=");
    uart_put_mv_as_volts(mv);
    uart_puts("\r\n");

    return SAMPLE_PERIOD_MS;
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
