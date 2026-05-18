/*
 * KTOS - Arduino Pro Mini SPI loopback example (bare-metal)
 *
 * A single KTOS task drives the ATmega328P's hardware SPI peripheral
 * in master mode and runs a loopback self-test once per second.
 *
 *   KTOS_MSG_TYPE_INIT   -> banner, configure SPI, do one immediate test
 *   KTOS_MSG_TYPE_TIMER  -> repeat the test, sleep 1000 ms
 *
 * Connect MOSI (D11 / PB3) to MISO (D12 / PB4) with a single jumper
 * wire; every byte the task transmits should come back to it.
 *
 * No Arduino framework, no SPI.h - direct register access for SPCR,
 * SPSR, SPDR, plus DDRB/PORTB for the chip-select line.
 *
 * Note: SCK is on PB5 = D13 = the on-board LED.  The LED will flicker
 * briefly during each SPI transfer; that is expected.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <stdbool.h>

#include "../../../../../core/ktos.h"

/* =========================================================================
 * USART0 - 115200 8N1 (U2X0=1, UBRR0=16 for 16 MHz F_CPU)
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

static void uart_putu16(uint16_t n)
{
    char buf[6];
    uint8_t i = 0;
    if (n == 0) { uart_putc('0'); return; }
    while (n) { buf[i++] = (char)('0' + (n % 10)); n /= 10; }
    while (i--) { uart_putc(buf[i]); }
}

static void uart_put_nibble(uint8_t n)
{
    n &= 0x0F;
    uart_putc((char)(n < 10 ? ('0' + n) : ('A' + n - 10)));
}

static void uart_put_hex2(uint8_t b)
{
    uart_put_nibble((uint8_t)(b >> 4));
    uart_put_nibble(b);
}

/* =========================================================================
 * SPI master - 1 MHz (F_CPU / 16) at 16 MHz, mode 0, MSB first
 *
 * Pin layout on the ATmega328P (Pro Mini header):
 *   D11 / PB3 = MOSI   (master out)
 *   D12 / PB4 = MISO   (master in)
 *   D13 / PB5 = SCK    (also the on-board LED — will flicker on transfer)
 *   D10 / PB2 = SS     (output, asserted low during transfer)
 *
 * The SS pin MUST be configured as an output even if the slave's CS
 * is wired elsewhere — otherwise an external low pulse on PB2 would
 * drop the AVR into SPI slave mode.
 * ========================================================================= */

#define SPI_PORT  PORTB
#define SPI_DDR   DDRB
#define SPI_MOSI  PORTB3
#define SPI_MISO  PORTB4
#define SPI_SCK   PORTB5
#define SPI_SS    PORTB2

static void spi_master_init(void)
{
    /* MOSI, SCK, SS as outputs; MISO as input. */
    SPI_DDR  |=  (1 << SPI_MOSI) | (1 << SPI_SCK) | (1 << SPI_SS);
    SPI_DDR  &= (uint8_t)~(1 << SPI_MISO);

    /* Idle SS high. */
    SPI_PORT |= (1 << SPI_SS);

    /* Enable SPI, master, MSB-first, mode 0 (CPOL=0, CPHA=0),
     * F_osc / 16 = 1 MHz (SPR1:0 = 01, SPI2X = 0). */
    SPCR = (1 << SPE) | (1 << MSTR) | (1 << SPR0);
    SPSR = 0;
}

static inline void spi_cs_low(void)  { SPI_PORT &= (uint8_t)~(1 << SPI_SS); }
static inline void spi_cs_high(void) { SPI_PORT |=             (1 << SPI_SS); }

static uint8_t spi_transfer(uint8_t out)
{
    SPDR = out;
    /* Busy-wait on transfer complete — at 1 MHz this is ~8 us per byte. */
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

/* =========================================================================
 * KTOS 1 ms tick - Timer1 CTC
 * ========================================================================= */

extern void ktos_timer_irq_handler(void);

ISR(TIMER1_COMPA_vect)
{
    ktos_timer_irq_handler();
}

/* =========================================================================
 * SPI loopback task
 * ========================================================================= */

#define TEST_PERIOD_MS  1000U

static const uint8_t TEST_PATTERN[] = {
    0xAAu, 0x55u, 0xFFu, 0x00u, 0x12u, 0x34u, 0xDEu, 0xADu
};
#define TEST_LEN  ((uint8_t)(sizeof TEST_PATTERN / sizeof TEST_PATTERN[0]))

static uint16_t g_iteration = 0;

static void run_loopback_test(void)
{
    ++g_iteration;
    uart_puts("Iter ");
    uart_putu16(g_iteration);
    uart_puts(": ");

    uint8_t mismatches = 0;
    uint8_t first_bad_index = 0;
    uint8_t first_bad_got   = 0;

    spi_cs_low();
    for (uint8_t i = 0; i < TEST_LEN; ++i) {
        uint8_t out = TEST_PATTERN[i];
        uint8_t in  = spi_transfer(out);
        if (in != out) {
            if (mismatches == 0) {
                first_bad_index = i;
                first_bad_got   = in;
            }
            ++mismatches;
        }
    }
    spi_cs_high();

    if (mismatches == 0) {
        uart_puts("PASS  (8/8 bytes echoed)\r\n");
    } else {
        uart_puts("FAIL  ");
        uart_putu16(mismatches);
        uart_puts("/8 mismatches; first at byte ");
        uart_putu16(first_bad_index);
        uart_puts(" sent 0x");
        uart_put_hex2(TEST_PATTERN[first_bad_index]);
        uart_puts(" got 0x");
        uart_put_hex2(first_bad_got);
        uart_puts("\r\n");
    }
}

static WORD spi_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam;
    (void)lParam;

    switch (MsgType) {
        case KTOS_MSG_TYPE_INIT:
            uart_puts("=============================\r\n");
            uart_puts("  KTOS SPI Loopback Example\r\n");
            uart_puts("=============================\r\n");
            uart_puts("Jumper D11 (MOSI) <-> D12 (MISO) and watch each\r\n");
            uart_puts("iteration echo 8/8 bytes.  Pull the jumper to see FAIL.\r\n");
            run_loopback_test();
            break;

        case KTOS_MSG_TYPE_TIMER:
            run_loopback_test();
            break;
    }

    return TEST_PERIOD_MS;
}

/* =========================================================================
 * Entry point
 * ========================================================================= */

int main(void)
{
    uart_init();
    spi_master_init();

    ktos_InitTask(spi_task,
                  /* StackSize = */ 96,
                  /* QueueSize = */ 4,
                  /* TaskID    = */ 'S');

    ktos_RunOS();    /* never returns */
    return 0;        /* unreachable */
}
