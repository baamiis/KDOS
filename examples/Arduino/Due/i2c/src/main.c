/*
 * KTOS — Arduino Due I2C scanner example (bare-metal)
 *
 * Single KTOS task scans TWI1 (D20=SDA/PB12, D21=SCL/PB13) every 5 s.
 * A device ACKs the read probe if present; prints its 7-bit address.
 *
 * TWI1 = SAM3X8E peripheral ID 23.
 * Clock: CKDIV=2, CLDIV=CHDIV=104 → exactly 100 kHz at 84 MHz.
 *
 * Serial: connect via the PROGRAMMING port (small USB near reset).
 */

#include <stdint.h>
#include <stdbool.h>
#include "../../../../../core/ktos.h"

/* PMC */
#define PMC_PCER0  (*(volatile uint32_t *)0x400E0610UL)

/* PIOA — UART0 */
#define PIOA_PDR   (*(volatile uint32_t *)0x400E0E04UL)
#define PIOA_ABSR  (*(volatile uint32_t *)0x400E0E70UL)

/* PIOB — TWI1: PB12=SDA, PB13=SCL */
#define PIOB_PDR   (*(volatile uint32_t *)0x400E1004UL)
#define PIOB_ABSR  (*(volatile uint32_t *)0x400E1070UL)

/* UART0 */
#define UART0_CR   (*(volatile uint32_t *)0x400E0800UL)
#define UART0_MR   (*(volatile uint32_t *)0x400E0804UL)
#define UART0_SR   (*(volatile uint32_t *)0x400E0814UL)
#define UART0_THR  (*(volatile uint32_t *)0x400E081CUL)
#define UART0_BRGR (*(volatile uint32_t *)0x400E0820UL)
#define UART_SR_TXRDY (1u << 1)

static void uart_init(void)
{
    PMC_PCER0  = (1u << 11) | (1u << 8);
    PIOA_PDR   = (1u << 8) | (1u << 9);
    PIOA_ABSR &= ~((1u << 8) | (1u << 9));
    UART0_CR   = (1u << 2) | (1u << 3);
    UART0_MR   = (4u << 9);
    UART0_BRGR = 46u;
    UART0_CR   = (1u << 4) | (1u << 6);
}

static void uart_putc(char c) { while (!(UART0_SR & UART_SR_TXRDY)) {} UART0_THR = (uint8_t)c; }
static void uart_puts(const char *s) { while (*s) { uart_putc(*s++); } }

static void uart_put_hex2(uint8_t b)
{
    const char hex[] = "0123456789ABCDEF";
    uart_putc(hex[b >> 4]);
    uart_putc(hex[b & 0x0Fu]);
}

static void uart_putu8(uint8_t n)
{
    char buf[4]; uint8_t i = 0;
    if (!n) { uart_putc('0'); return; }
    while (n) { buf[i++] = (char)('0' + n % 10u); n = (uint8_t)(n / 10u); }
    while (i--) { uart_putc(buf[i]); }
}

/* =========================================================================
 * TWI1 — 100 kHz I2C master, D20=SDA (PB12), D21=SCL (PB13)
 * PMC ID 23.  CKDIV=2, CLDIV=CHDIV=104 → exactly 100 kHz at 84 MHz.
 * ========================================================================= */
#define TWI1_CR    (*(volatile uint32_t *)0x40090000UL)
#define TWI1_MMR   (*(volatile uint32_t *)0x40090004UL)
#define TWI1_IADR  (*(volatile uint32_t *)0x4009000CUL)
#define TWI1_CWGR  (*(volatile uint32_t *)0x40090010UL)
#define TWI1_SR    (*(volatile uint32_t *)0x40090020UL)
#define TWI1_IDR   (*(volatile uint32_t *)0x40090028UL)
#define TWI1_RHR   (*(volatile uint32_t *)0x40090030UL)

#define TWI_SR_TXCOMP (1u << 0)
#define TWI_SR_RXRDY  (1u << 1)
#define TWI_SR_NACK   (1u << 8)

static void twi_init(void)
{
    PMC_PCER0 |= (1u << 12) | (1u << 23);    /* PIOB (12) + TWI1 (23) */
    PIOB_ABSR &= ~((1u << 12) | (1u << 13)); /* peripheral A */
    PIOB_PDR   = (1u << 12) | (1u << 13);    /* hand to peripheral */
    TWI1_CR    = (1u << 7);                   /* SWRST */
    (void)TWI1_RHR;                           /* drain RHR */
    TWI1_CR    = (1u << 3) | (1u << 5);      /* MSDIS | SVDIS */
    TWI1_IDR   = 0xFFFFFFFFu;
    /* CKDIV=2, CLDIV=CHDIV=104 → (104×4+4)/84MHz = 5 µs → 100 kHz */
    TWI1_CWGR  = (2u << 16) | (104u << 8) | 104u;
    TWI1_CR    = (1u << 2);                   /* MSEN */
}

/* Returns true if a device ACKs a 1-byte read at the given 7-bit address. */
static bool twi_probe(uint8_t addr)
{
    TWI1_MMR  = ((uint32_t)addr << 16) | (1u << 12); /* DADR | MREAD */
    TWI1_IADR = 0u;
    TWI1_CR   = (1u << 0) | (1u << 1);               /* START | STOP */

    uint32_t sr;
    do {
        sr = TWI1_SR;
        if (sr & TWI_SR_NACK) {
            while (!(TWI1_SR & TWI_SR_TXCOMP)) {}
            return false;
        }
    } while (!(sr & TWI_SR_RXRDY) && !(sr & TWI_SR_TXCOMP));

    bool ack = (sr & TWI_SR_RXRDY) != 0u;
    if (ack) { (void)TWI1_RHR; }
    while (!(TWI1_SR & TWI_SR_TXCOMP)) {}
    return ack;
}

/* TC0 — KTOS tick */
#define TC0_CH0_SR (*(volatile uint32_t *)0x40080020UL)
void TC0_Handler(void) { (void)TC0_CH0_SR; ktos_timer_irq_handler(); }

__attribute__((noreturn))
void ktos_Emergency(const char *msg) { uart_puts("\r\n[KTOS FATAL] "); uart_puts(msg); while (1) {} }
void ktos_DebugPrintf(const char *f, ...) { (void)f; }
void ktos_InitSys(void) {}

#define SCAN_PERIOD_MS 5000U

static void run_scan(void)
{
    uart_puts("Scanning I2C bus (0x01-0x7E)...\r\n");
    uint8_t found = 0;
    for (uint8_t addr = 0x01u; addr <= 0x7Eu; ++addr) {
        if (twi_probe(addr)) {
            uart_puts("  Found: 0x");
            uart_put_hex2(addr);
            uart_puts("\r\n");
            ++found;
        }
    }
    if (!found) {
        uart_puts("No I2C devices found.\r\n");
    } else {
        uart_putu8(found);
        uart_puts(found == 1u ? " device found.\r\n" : " devices found.\r\n");
    }
    uart_puts("\r\n");
}

static WORD scanner_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam; (void)lParam;
    if (MsgType == KTOS_MSG_TYPE_INIT) {
        twi_init();
        uart_puts("=============================\r\n");
        uart_puts("  KTOS I2C Scanner\r\n");
        uart_puts("  Arduino Due (SAM3X8E)\r\n");
        uart_puts("  SDA=D20 (PB12)  SCL=D21 (PB13)\r\n");
        uart_puts("  TWI1, 100 kHz\r\n");
        uart_puts("=============================\r\n");
        run_scan();
        return SCAN_PERIOD_MS;
    }
    run_scan();
    return SCAN_PERIOD_MS;
}

int main(void)
{
    uart_init();
    ktos_InitTask(scanner_task, 256, 4, 'I');
    ktos_RunOS();
    return 0;
}
