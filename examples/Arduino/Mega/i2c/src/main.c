/*
 * KTOS — Arduino Mega 2560 I2C scanner example (bare-metal)
 *
 * One KTOS task scans the I2C bus from 0x01..0x7E:
 *
 *   KTOS_MSG_TYPE_INIT   -> banner + TWI init, immediate first scan
 *   KTOS_MSG_TYPE_TIMER  -> rescan every 5000 ms
 *
 * SDA = D20 (PD1), SCL = D21 (PD0) on the Mega 2560.
 * TWI runs at 100 kHz from a 16 MHz F_CPU.
 * No Arduino framework, no Wire library — just registers.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <stdbool.h>

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

static void uart_puthex8(uint8_t v)
{
    const char *hex = "0123456789ABCDEF";
    uart_putc(hex[v >> 4]);
    uart_putc(hex[v & 0x0F]);
}

/* =========================================================================
 * TWI (I2C) — 100 kHz at 16 MHz
 * TWBR = ((F_CPU / F_SCL) - 16) / (2 * prescaler) = (16000000/100000 - 16) / 2 = 72
 * ========================================================================= */

static void twi_init(void)
{
    TWSR = 0x00;    /* prescaler = 1 */
    TWBR = 72;      /* 100 kHz at 16 MHz */
    TWCR = (1 << TWEN);
}

static bool twi_start(uint8_t addr_rw)
{
    TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT))) { }
    if ((TWSR & 0xF8) != 0x08 && (TWSR & 0xF8) != 0x10) { return false; }

    TWDR = addr_rw;
    TWCR = (1 << TWINT) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT))) { }

    uint8_t status = TWSR & 0xF8;
    return (status == 0x18 || status == 0x40);  /* SLA+W ACK or SLA+R ACK */
}

static void twi_stop(void)
{
    TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
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
 * I2C scan task
 * ========================================================================= */

static void do_scan(void)
{
    uint8_t found = 0;
    uart_puts("Scanning I2C bus...\r\n");

    for (uint8_t addr = 0x01; addr <= 0x7E; addr++) {
        if (twi_start((uint8_t)(addr << 1))) {
            uart_puts("  Found: 0x");
            uart_puthex8(addr);
            uart_puts("\r\n");
            found++;
        }
        twi_stop();
    }

    if (found == 0) {
        uart_puts("  No devices found.\r\n");
    }
}

static WORD i2c_task(WORD MsgType, WORD Param1, LONG Param2)
{
    (void)Param1;
    (void)Param2;

    if (MsgType == KTOS_MSG_TYPE_INIT) {
        twi_init();
        uart_puts("=============================\r\n");
        uart_puts("  KTOS I2C Scanner\r\n");
        uart_puts("  Arduino Mega 2560\r\n");
        uart_puts("  SDA=D20, SCL=D21\r\n");
        uart_puts("=============================\r\n");
        do_scan();
        return 5000;
    }

    do_scan();
    return 5000;
}

/* =========================================================================
 * Entry point
 * ========================================================================= */

int main(void)
{
    uart_init();
    ktos_InitTask(i2c_task, 128, 4, 'I');
    ktos_RunOS();
    return 0;
}
