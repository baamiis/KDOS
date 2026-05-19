/*
 * KTOS — Arduino Due SPI loopback example (bare-metal)
 *
 * Single KTOS task exercises SPI0 in hardware-loopback mode (LLB bit).
 * Sends bytes 0x00–0xFF every 2 s and prints PASS / error count.
 * No external wiring required — the LLB bit internally connects MOSI to MISO.
 *
 * SPI0 pins (all peripheral A on PIOA):
 *   PA25 = MISO  (ICSP-1 / D50)
 *   PA26 = MOSI  (ICSP-4 / D51)
 *   PA27 = SPCK  (ICSP-3 / D52)
 *   PA28 = NPCS0 (D10 / D53 — hardware chip-select)
 *
 * Clock: 84 MHz / SCBR=84 = 1 MHz, SPI mode 0 (CPOL=0, NCPHA=1).
 * PMC peripheral ID: 24.
 *
 * Serial: connect via the PROGRAMMING port (small USB near reset).
 */

#include <stdint.h>
#include "../../../../../core/ktos.h"

/* PMC */
#define PMC_PCER0  (*(volatile uint32_t *)0x400E0610UL)

/* PIOA — UART0 (PA8/PA9) and SPI0 (PA25-PA28) */
#define PIOA_PDR   (*(volatile uint32_t *)0x400E0E04UL)
#define PIOA_ABSR  (*(volatile uint32_t *)0x400E0E70UL)

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

static void uart_putu32(uint32_t n)
{
    char buf[10]; uint8_t i = 0;
    if (!n) { uart_putc('0'); return; }
    while (n) { buf[i++] = (char)('0' + n % 10u); n /= 10u; }
    while (i--) { uart_putc(buf[i]); }
}

/* =========================================================================
 * SPI0 — 1 MHz, mode 0, hardware loopback (LLB bit in SPI_MR)
 * PA25=MISO, PA26=MOSI, PA27=SPCK, PA28=NPCS0 — all peripheral A.
 * PMC ID 24.  SCBR=84 → 84 MHz / 84 = 1 MHz.
 * ========================================================================= */
#define SPI0_CR    (*(volatile uint32_t *)0x40008000UL)
#define SPI0_MR    (*(volatile uint32_t *)0x40008004UL)
#define SPI0_RDR   (*(volatile uint32_t *)0x40008008UL)
#define SPI0_TDR   (*(volatile uint32_t *)0x4000800CUL)
#define SPI0_SR    (*(volatile uint32_t *)0x40008010UL)
#define SPI0_CSR0  (*(volatile uint32_t *)0x40008030UL)

#define SPI_SR_RDRF  (1u << 0)
#define SPI_SR_TDRE  (1u << 1)

static void spi_init(void)
{
    PMC_PCER0 |= (1u << 24);   /* SPI0 clock (ID 24) */
    /* PA25/PA26/PA27/PA28 → peripheral A */
    PIOA_ABSR &= ~((1u << 25) | (1u << 26) | (1u << 27) | (1u << 28));
    PIOA_PDR   = (1u << 25) | (1u << 26) | (1u << 27) | (1u << 28);

    SPI0_CR  = (1u << 7);   /* SWRST */
    SPI0_CR  = (1u << 7);   /* SWRST again (errata: two resets required) */
    SPI0_CR  = (1u << 1);   /* SPIDIS */
    /* MSTR(0) | LLB(7) | MODFDIS(4) | PCS=0b1110<<16 (NPCS0 active) */
    SPI0_MR  = (1u << 0) | (1u << 7) | (1u << 4) | (0x0Eu << 16);
    /* CSR0: NCPHA=1 (mode 0), SCBR=84 (1 MHz) */
    SPI0_CSR0 = (84u << 8) | (1u << 1);
    SPI0_CR  = (1u << 0);   /* SPIEN */
}

static uint8_t spi_transfer(uint8_t b)
{
    while (!(SPI0_SR & SPI_SR_TDRE)) {}
    SPI0_TDR = b;
    while (!(SPI0_SR & SPI_SR_RDRF)) {}
    return (uint8_t)(SPI0_RDR & 0xFFu);
}

/* TC0 — KTOS tick */
#define TC0_CH0_SR (*(volatile uint32_t *)0x40080020UL)
void TC0_Handler(void) { (void)TC0_CH0_SR; ktos_timer_irq_handler(); }

__attribute__((noreturn))
void ktos_Emergency(const char *msg) { uart_puts("\r\n[KTOS FATAL] "); uart_puts(msg); while (1) {} }
void ktos_DebugPrintf(const char *f, ...) { (void)f; }
void ktos_InitSys(void) {}

#define TEST_PERIOD_MS 2000U

static WORD spi_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam; (void)lParam;
    if (MsgType == KTOS_MSG_TYPE_INIT) {
        spi_init();
        uart_puts("=============================\r\n");
        uart_puts("  KTOS SPI Loopback\r\n");
        uart_puts("  Arduino Due (SAM3X8E)\r\n");
        uart_puts("  SPI0, 1 MHz, hardware LLB\r\n");
        uart_puts("  (no external wiring needed)\r\n");
        uart_puts("=============================\r\n");
        return TEST_PERIOD_MS;
    }

    uint32_t errors = 0;
    for (uint16_t i = 0u; i <= 0xFFu; ++i) {
        uint8_t sent = (uint8_t)i;
        uint8_t recv = spi_transfer(sent);
        if (recv != sent) {
            uart_puts("  ERR sent=0x");
            uart_put_hex2(sent);
            uart_puts(" got=0x");
            uart_put_hex2(recv);
            uart_puts("\r\n");
            ++errors;
        }
    }

    uart_puts("SPI loopback 0x00-0xFF: ");
    if (errors == 0u) {
        uart_puts("PASS\r\n");
    } else {
        uart_putu32(errors);
        uart_puts(" error(s) — FAIL\r\n");
    }
    return TEST_PERIOD_MS;
}

int main(void)
{
    uart_init();
    ktos_InitTask(spi_task, 128, 4, 'S');
    ktos_RunOS();
    return 0;
}
