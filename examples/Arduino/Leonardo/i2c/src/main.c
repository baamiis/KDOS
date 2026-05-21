/*
 * KTOS — Arduino Leonardo I2C scanner example (bare-metal)
 *
 * One KTOS task scans the I2C bus from 0x01..0x7E:
 *
 *   KTOS_MSG_TYPE_INIT   -> banner + TWI init, immediate first scan
 *   KTOS_MSG_TYPE_TIMER  -> rescan every 5000 ms
 *
 * ATmega32U4 TWI pin mapping:
 *   SDA = D2 / PD1
 *   SCL = D3 / PD0
 *
 * TWI runs at 100 kHz from a 16 MHz F_CPU.
 * No Arduino framework, no Wire library — just registers.
 *
 * Serial output uses USART1 on D0(RX)/D1(TX).  Connect a USB-to-serial
 * adapter — adapter-RX → D1, adapter-TX → D0.
 *
 * Pin summary to avoid confusion on the Leonardo header:
 *   D0 = PD2 = USART1 RX
 *   D1 = PD3 = USART1 TX
 *   D2 = PD1 = TWI SDA  <-- I2C
 *   D3 = PD0 = TWI SCL  <-- I2C
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

static void uart_put_hex2(uint8_t b)
{
    const char *hex = "0123456789ABCDEF";
    uart_putc(hex[b >> 4]);
    uart_putc(hex[b & 0x0F]);
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
 * TWI (I2C) — 100 kHz at 16 MHz
 * SDA = PD1 = D2,  SCL = PD0 = D3
 * TWBR = ((16000000/100000) - 16) / 2 = 72
 * ========================================================================= */

#define TWI_STATUS_MASK           0xF8
#define TWI_STATUS_START          0x08
#define TWI_STATUS_REPEATED_START 0x10
#define TWI_STATUS_SLA_W_ACK      0x18

static void twi_init(void)
{
    PORTD |= (1 << PD0) | (1 << PD1);  /* internal pull-ups on SCL/SDA */
    TWSR = 0;
    TWBR = 72;
    TWCR = (1 << TWEN);
}

static void twi_stop(void)
{
    TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWSTO);
}

static bool twi_probe(uint8_t addr_7bit)
{
    TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWSTA);
    while (!(TWCR & (1 << TWINT))) { }
    uint8_t status = TWSR & TWI_STATUS_MASK;
    if (status != TWI_STATUS_START && status != TWI_STATUS_REPEATED_START) {
        twi_stop();
        return false;
    }

    TWDR = (uint8_t)(addr_7bit << 1);
    TWCR = (1 << TWINT) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT))) { }
    bool ack = ((TWSR & TWI_STATUS_MASK) == TWI_STATUS_SLA_W_ACK);

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

ISR(TIMER1_COMPA_vect) { ktos_timer_irq_handler(); }

/* =========================================================================
 * Scanner task
 * ========================================================================= */

#define I2C_ADDR_MIN   0x01
#define I2C_ADDR_MAX   0x7E
#define SCAN_PERIOD_MS 5000U

static void run_scan(void)
{
    uart_puts("Scanning I2C bus...\r\n");
    uint8_t found = 0;

    for (uint8_t addr = I2C_ADDR_MIN; addr <= I2C_ADDR_MAX; ++addr) {
        if (twi_probe(addr)) {
            uart_puts("  Found: 0x");
            uart_put_hex2(addr);
            uart_puts("\r\n");
            ++found;
        }
    }

    if (found == 0) {
        uart_puts("  No devices found.\r\n");
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

    if (MsgType == KTOS_MSG_TYPE_INIT) {
        twi_init();
        uart_puts("=============================\r\n");
        uart_puts("  KTOS I2C Scanner\r\n");
        uart_puts("  Arduino Leonardo\r\n");
        uart_puts("  SDA=D2 (PD1), SCL=D3 (PD0)\r\n");
        uart_puts("=============================\r\n");
        run_scan();
        return SCAN_PERIOD_MS;
    }

    run_scan();
    return SCAN_PERIOD_MS;
}

/* =========================================================================
 * Entry point
 * ========================================================================= */

int main(void)
{
    uart_init();
    ktos_InitTask(scanner_task, 96, 4, 'I');
    ktos_RunOS();
    return 0;
}
