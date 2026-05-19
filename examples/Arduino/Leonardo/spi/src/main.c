/*
 * KTOS — Arduino Leonardo SPI loopback example (bare-metal)
 *
 * A single KTOS task drives the ATmega32U4's hardware SPI peripheral
 * in master mode and runs a loopback self-test once per second.
 *
 *   KTOS_MSG_TYPE_INIT   -> banner, configure SPI, do one immediate test
 *   KTOS_MSG_TYPE_TIMER  -> repeat the test, sleep 1000 ms
 *
 * ATmega32U4 SPI pins — all on the 6-pin ICSP header:
 *   SS   = PB0        (not exposed on standard digital header)
 *   SCK  = PB1        ICSP pin 3
 *   MOSI = PB2        ICSP pin 4
 *   MISO = PB3        ICSP pin 1  (input)
 *
 * ICSP header pin-out (top view, notch at top-left):
 *   [ MISO  VCC  ]   pins 1  2
 *   [ SCK   MOSI ]   pins 3  4
 *   [ RESET GND  ]   pins 5  6
 *
 * Connect a jumper wire between ICSP-4 (MOSI/PB2) and ICSP-1 (MISO/PB3).
 *
 * D13 on the Leonardo is PC7 — NOT a SPI pin — so the LED does not
 * flicker during transfers (unlike the UNO where D13 = SCK = LED).
 *
 * Serial output uses USART1 on D0(RX)/D1(TX).  Connect a USB-to-serial
 * adapter — adapter-RX → D1, adapter-TX → D0.
 *
 * No Arduino framework, no SPI.h — direct register access only.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <stdbool.h>

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

static void uart_puthex8(uint8_t v)
{
    const char *hex = "0123456789ABCDEF";
    uart_putc(hex[v >> 4]);
    uart_putc(hex[v & 0x0F]);
}

/* =========================================================================
 * SPI master — ATmega32U4 / Leonardo ICSP header
 *   SS   = PB0
 *   SCK  = PB1 (ICSP-3)
 *   MOSI = PB2 (ICSP-4)
 *   MISO = PB3 (ICSP-1, input)
 * ========================================================================= */

#define SPI_SS_BIT    PB0
#define SPI_SCK_BIT   PB1
#define SPI_MOSI_BIT  PB2
#define SPI_MISO_BIT  PB3

static void spi_init(void)
{
    DDRB |= (1 << SPI_SS_BIT) | (1 << SPI_SCK_BIT) | (1 << SPI_MOSI_BIT);
    DDRB &= (uint8_t)~(1 << SPI_MISO_BIT);
    PORTB |= (1 << SPI_SS_BIT);   /* SS idle high */
    SPCR = (1 << SPE) | (1 << MSTR) | (1 << SPR0);  /* SPI master, F_CPU/16 */
}

static uint8_t spi_transfer(uint8_t data)
{
    SPDR = data;
    while (!(SPSR & (1 << SPIF))) { }
    return SPDR;
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
 * Test pattern
 * ========================================================================= */

static const uint8_t k_pattern[] = { 0xA5, 0x5A, 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0xFF };
#define PATTERN_LEN ((uint8_t)(sizeof(k_pattern) / sizeof(k_pattern[0])))

static void run_loopback(void)
{
    uint8_t errors = 0;

    PORTB &= (uint8_t)~(1 << SPI_SS_BIT);   /* assert SS */

    for (uint8_t i = 0; i < PATTERN_LEN; i++) {
        uint8_t rx = spi_transfer(k_pattern[i]);
        if (rx != k_pattern[i]) { errors++; }
    }

    PORTB |= (1 << SPI_SS_BIT);             /* deassert SS */

    if (errors == 0) {
        uart_puts("Loopback OK  (");
        uart_puthex8(PATTERN_LEN);
        uart_puts(" bytes)\r\n");
    } else {
        uart_puts("Loopback FAIL  errors=");
        uart_puthex8(errors);
        uart_puts("  (ICSP-4 -> ICSP-1 jumper connected?)\r\n");
    }
}

/* =========================================================================
 * SPI task
 * ========================================================================= */

static WORD spi_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam;
    (void)lParam;

    if (MsgType == KTOS_MSG_TYPE_INIT) {
        spi_init();
        uart_puts("=============================\r\n");
        uart_puts("  KTOS SPI Loopback\r\n");
        uart_puts("  Arduino Leonardo\r\n");
        uart_puts("  MOSI=ICSP-4, MISO=ICSP-1\r\n");
        uart_puts("  SCK=ICSP-3   SS=PB0\r\n");
        uart_puts("  Connect ICSP-4 to ICSP-1\r\n");
        uart_puts("=============================\r\n");
        run_loopback();
        return 1000;
    }

    run_loopback();
    return 1000;
}

/* =========================================================================
 * Entry point
 * ========================================================================= */

int main(void)
{
    uart_init();
    ktos_InitTask(spi_task, 96, 4, 'S');
    ktos_RunOS();
    return 0;
}
