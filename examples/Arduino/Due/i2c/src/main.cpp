/*
 * KTOS - Arduino Due I2C scanner example
 *
 * A single KTOS task scans the TWI bus from 0x01..0x7E.  Uses TWI1
 * (Arduino label SDA/SCL on pins 20/21, mapped to PB12/PB13).  This
 * is the "Wire" bus in the standard Arduino-SAM mapping.
 *
 *   KTOS_MSG_TYPE_INIT   -> banner + TWI init, immediate first scan
 *   KTOS_MSG_TYPE_TIMER  -> rescan, sleep 5000 ms
 *
 * No Arduino API - direct register access to TWI1, PIOB, PMC.
 */

#include "sam.h"
#include <stdint.h>
#include <stdbool.h>

extern "C" {
#include "../../../../../core/ktos.h"
}

/* =========================================================================
 * Programming Port UART
 * ========================================================================= */

static void uart_init(void)
{
    PMC->PMC_PCER0  = (1u << ID_UART);
    PIOA->PIO_ABSR &= ~(PIO_PA8 | PIO_PA9);
    PIOA->PIO_PDR   =  (PIO_PA8 | PIO_PA9);
    UART->UART_CR   = UART_CR_RSTRX | UART_CR_RSTTX | UART_CR_RXDIS | UART_CR_TXDIS;
    UART->UART_MR   = UART_MR_PAR_NO | UART_MR_CHMODE_NORMAL;
    UART->UART_PTCR = UART_PTCR_RXTDIS | UART_PTCR_TXTDIS;
    UART->UART_IDR  = 0xFFFFFFFFu;
    UART->UART_BRGR = 46;
    UART->UART_CR   = UART_CR_RXEN | UART_CR_TXEN;
}

static void uart_putc(char c)
{
    while (!(UART->UART_SR & UART_SR_TXRDY)) { }
    UART->UART_THR = (uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s) { uart_putc(*s++); }
}

static void uart_put_nibble(uint8_t n)
{
    n &= 0x0Fu;
    uart_putc((char)(n < 10 ? ('0' + n) : ('A' + n - 10)));
}

static void uart_put_hex2(uint8_t b)
{
    uart_put_nibble((uint8_t)(b >> 4));
    uart_put_nibble(b);
}

static void uart_putu8(uint8_t n)
{
    char buf[4];
    uint8_t i = 0;
    if (n == 0) { uart_putc('0'); return; }
    while (n) { buf[i++] = (char)('0' + (n % 10)); n /= 10; }
    while (i--) { uart_putc(buf[i]); }
}

/* =========================================================================
 * TWI1 (Arduino "Wire" on the Due) - 100 kHz at 84 MHz MCK
 *
 * Pins: PB12 = TWD1 (SDA), PB13 = TWCK1 (SCL) — both peripheral A.
 * Clock period formula (datasheet §33.7.2):
 *   t_low  = ((CLDIV * 2^CKDIV) + 4) * t_MCK
 *   t_high = ((CHDIV * 2^CKDIV) + 4) * t_MCK
 *   t_bit  = t_low + t_high
 *
 * For 100 kHz at MCK=84 MHz: CKDIV=2, CLDIV=CHDIV=103 → ~99.7 kHz, 0.3% slow.
 * ========================================================================= */

static void twi_init(void)
{
    /* Enable PMC clocks: PIOB (clock 12), TWI1 (peripheral ID 23). */
    PMC->PMC_PCER0 = (1u << ID_PIOB) | (1u << ID_TWI1);

    /* Route PB12/PB13 to peripheral A (TWI1). */
    PIOB->PIO_ABSR &= ~(PIO_PB12 | PIO_PB13);
    PIOB->PIO_PDR   =  (PIO_PB12 | PIO_PB13);

    /* Reset TWI1. */
    TWI1->TWI_CR = TWI_CR_SWRST;
    (void)TWI1->TWI_RHR;     /* dummy read clears RXRDY */

    /* Disable PDC; mask interrupts. */
    TWI1->TWI_CR = TWI_CR_SVDIS | TWI_CR_MSDIS;
    TWI1->TWI_IDR = 0xFFFFFFFFu;

    /* 100 kHz: CKDIV=2, CLDIV=CHDIV=103. */
    TWI1->TWI_CWGR = TWI_CWGR_CKDIV(2) | TWI_CWGR_CLDIV(103) | TWI_CWGR_CHDIV(103);

    /* Master mode. */
    TWI1->TWI_CR = TWI_CR_MSEN;
}

/* Probe a 7-bit address; return true if the slave ACKs.
 *
 * Strategy: queue a 1-byte read with START + STOP, then check whether
 * any byte arrives (RXRDY) or only the NACK error appears in TWI_SR. */
static bool twi_probe(uint8_t addr)
{
    /* DADR = address, MREAD (read), 1-byte internal address size = 0. */
    TWI1->TWI_MMR = TWI_MMR_DADR(addr) | TWI_MMR_MREAD;
    TWI1->TWI_IADR = 0;

    /* START + STOP — minimal handshake. */
    TWI1->TWI_CR = TWI_CR_START | TWI_CR_STOP;

    /* Wait until the transaction completes (TXCOMP) or we see a NACK. */
    uint32_t status;
    do {
        status = TWI1->TWI_SR;
        if (status & TWI_SR_NACK) {
            /* NACK means no device at this address. Drain TXCOMP. */
            while (!(TWI1->TWI_SR & TWI_SR_TXCOMP)) { }
            return false;
        }
    } while (!(status & TWI_SR_RXRDY) && !(status & TWI_SR_TXCOMP));

    /* If RXRDY ever fired, a byte arrived — device ACKed. */
    bool ack = (status & TWI_SR_RXRDY) != 0;
    if (ack) {
        (void)TWI1->TWI_RHR;   /* drain the byte */
    }
    while (!(TWI1->TWI_SR & TWI_SR_TXCOMP)) { }
    return ack;
}

/* =========================================================================
 * KTOS callbacks + tick ISR
 * ========================================================================= */

extern "C" __attribute__((noreturn))
void ktos_Emergency(const char *msg)
{
    uart_puts("\r\n[KTOS FATAL] ");
    uart_puts(msg);
    uart_puts("\r\n");
    while (1) { }
}

extern "C" void ktos_DebugPrintf(const char *fmt, ...) { (void)fmt; }
extern "C" void ktos_InitSys(void) { }

extern "C" void ktos_timer_irq_handler(void);

extern "C" void TC0_Handler(void)
{
    (void)TC0->TC_CHANNEL[0].TC_SR;
    ktos_timer_irq_handler();
}

/* =========================================================================
 * Scanner task
 * ========================================================================= */

#define I2C_ADDR_MIN   0x01u
#define I2C_ADDR_MAX   0x7Eu
#define SCAN_PERIOD_MS 5000u

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

static WORD scanner_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam;
    (void)lParam;

    switch (MsgType) {
        case KTOS_MSG_TYPE_INIT:
            uart_puts("=============================\r\n");
            uart_puts("  KTOS I2C Scanner (Arduino Due)\r\n");
            uart_puts("=============================\r\n");
            run_scan();
            break;
        case KTOS_MSG_TYPE_TIMER:
            run_scan();
            break;
    }
    return SCAN_PERIOD_MS;
}

extern "C" void setup(void)
{
    WDT->WDT_MR = WDT_MR_WDDIS;
    uart_init();
    twi_init();

    ktos_InitTask(scanner_task, 256, 4, 'I');
    ktos_RunOS();
}

extern "C" void loop(void) { }
