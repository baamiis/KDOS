/*
 * KTOS — Arduino Nano I2C scanner example (bare-metal)
 *
 * One KTOS task scans the I2C bus from 0x01..0x7E:
 *
 *   KTOS_MSG_TYPE_INIT   -> banner + TWI init, immediate first scan
 *   KTOS_MSG_TYPE_TIMER  -> rescan, sleep 5000 ms
 *
 * SDA = A4 (PC4), SCL = A5 (PC5).  TWI runs at 100 kHz from a 16 MHz
 * F_CPU.  No Arduino framework, no Wire library — just registers.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <stdbool.h>

#include "../../../../../core/ktos.h"

/* =========================================================================
 * USART0 — 115200 8N1 (U2X0=1, UBRR0=16 → 117647 baud, 2.1% error)
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

/* Print a single hex nibble (0..15). */
static void uart_put_nibble(uint8_t n)
{
    n &= 0x0F;
    uart_putc((char)(n < 10 ? ('0' + n) : ('A' + n - 10)));
}

/* Print a byte as exactly two hex digits, e.g. 0x3C -> "3C". */
static void uart_put_hex2(uint8_t b)
{
    uart_put_nibble((uint8_t)(b >> 4));
    uart_put_nibble(b);
}

static void uart_putu8(uint8_t n)
{
    char buf[3];
    uint8_t i = 0;
    if (n == 0) { uart_putc('0'); return; }
    while (n) { buf[i++] = (char)('0' + (n % 10)); n /= 10; }
    while (i--) { uart_putc(buf[i]); }
}

/* =========================================================================
 * TWI (I2C) — 100 kHz at F_CPU = 16 MHz
 *
 * SCL freq = F_CPU / (16 + 2*TWBR*prescaler)
 * Prescaler 1 (TWSR bits TWPS1:0 = 0), TWBR = 72  -> 100 kHz.
 * ========================================================================= */

#define TWI_STATUS_MASK            0xF8
#define TWI_STATUS_START           0x08
#define TWI_STATUS_REPEATED_START  0x10
#define TWI_STATUS_SLA_W_ACK       0x18

static void twi_init(void)
{
    /* Internal pull-ups on PC4/PC5 give a weak fallback if no external
     * pull-ups are present.  External 4.7 kΩ to 5V is still recommended. */
    PORTC |= (1 << PORTC4) | (1 << PORTC5);

    TWSR = 0;            /* prescaler = 1 */
    TWBR = 72;           /* 100 kHz */
    TWCR = (1 << TWEN);  /* enable TWI peripheral */
}

static void twi_stop(void)
{
    TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWSTO);
    /* TWSTO clears automatically when the STOP completes — no flag to poll. */
}

/* Probe a 7-bit address: returns true if the slave ACKs SLA+W. */
static bool twi_probe(uint8_t addr_7bit)
{
    /* START */
    TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWSTA);
    while (!(TWCR & (1 << TWINT))) { }
    uint8_t status = TWSR & TWI_STATUS_MASK;
    if (status != TWI_STATUS_START && status != TWI_STATUS_REPEATED_START) {
        twi_stop();
        return false;
    }

    /* SLA + W */
    TWDR = (uint8_t)(addr_7bit << 1);
    TWCR = (1 << TWINT) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT))) { }
    status = TWSR & TWI_STATUS_MASK;
    bool ack = (status == TWI_STATUS_SLA_W_ACK);

    twi_stop();
    return ack;
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
 * KTOS 1 ms tick — Timer1 CTC
 * ========================================================================= */

extern void ktos_timer_irq_handler(void);

ISR(TIMER1_COMPA_vect)
{
    ktos_timer_irq_handler();
}

/* =========================================================================
 * Scanner task
 * ========================================================================= */

#define I2C_ADDR_MIN     0x01
#define I2C_ADDR_MAX     0x7E
#define SCAN_PERIOD_MS   5000U

static void run_scan(void)
{
    uart_puts("Scanning I2C bus...\r\n");

    uint8_t found = 0;
    for (uint8_t addr = I2C_ADDR_MIN; addr <= I2C_ADDR_MAX; ++addr) {
        if (twi_probe(addr)) {
            uart_puts("Device found at 0x");
            uart_put_hex2(addr);
            uart_puts("\r\n");
            ++found;
        }
    }

    if (found == 0) {
        uart_puts("No I2C devices found.\r\n");
    } else {
        uart_puts("Scan complete (");
        uart_putu8(found);
        uart_puts(found == 1 ? " device).\r\n" : " devices).\r\n");
    }
    uart_puts("\r\n");
}

static WORD scanner_task(WORD MsgType, WORD Param1, LONG Param2)
{
    (void)Param1;
    (void)Param2;

    switch (MsgType) {
        case KTOS_MSG_TYPE_INIT:
            uart_puts("=============================\r\n");
            uart_puts("  KTOS I2C Scanner Example\r\n");
            uart_puts("=============================\r\n");
            run_scan();                /* first scan immediately */
            break;

        case KTOS_MSG_TYPE_TIMER:
            run_scan();                /* every 5 s thereafter */
            break;
    }

    return SCAN_PERIOD_MS;
}

/* =========================================================================
 * Entry point
 * ========================================================================= */

int main(void)
{
    uart_init();
    twi_init();

    ktos_InitTask(scanner_task,
                  /* StackSize  = */ 96,
                  /* QueueSize  = */ 4,
                  /* TaskID     = */ 'I');

    ktos_RunOS();        /* never returns */
    return 0;            /* unreachable */
}
