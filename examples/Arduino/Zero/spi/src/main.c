/*
 * KTOS — Arduino Zero SPI loopback example (bare-metal)
 *
 * Single KTOS task exercises SERCOM1 SPI master in hardware-loopback mode
 * (CTRLB.LOOPBACK=1).  Sends bytes 0x00–0xFF every 2 s, prints PASS / fail.
 * No external wiring required.
 *
 * SPI pins (SERCOM1, all peripheral C on PORT A):
 *   PA16 = MOSI  (SERCOM1 PAD[0]) = D11
 *   PA17 = SCK   (SERCOM1 PAD[1]) = D13
 *   PA19 = MISO  (SERCOM1 PAD[3]) = D12
 *
 * Clock: 48 MHz / BAUD-div → 1 MHz (BAUD register = 23).
 * GCLK ID 20 (SERCOM1_CORE), APBCMASK bit 3.
 *
 * Serial: PROGRAMMING port via EDBG → SERCOM5 (PA22=TX, PA23=RX).
 */

#include <stdint.h>
#include "../../../../../core/ktos.h"

/* PM / GCLK */
#define PM_APBCMASK  (*(volatile uint32_t *)0x40000420UL)
#define GCLK_CLKCTRL (*(volatile uint16_t *)0x40000C02UL)
#define GCLK_STATUS  (*(volatile uint8_t  *)0x40000C01UL)

/* PORT A */
#define PORTA_PMUX8    (*(volatile uint8_t  *)0x41004438UL) /* PA16/PA17 */
#define PORTA_PMUX9    (*(volatile uint8_t  *)0x41004439UL) /* PA18/PA19 */
#define PORTA_PMUX11   (*(volatile uint8_t  *)0x4100443BUL) /* PA22/PA23 */
#define PORTA_PINCFG16 (*(volatile uint8_t  *)0x41004450UL)
#define PORTA_PINCFG17 (*(volatile uint8_t  *)0x41004451UL)
#define PORTA_PINCFG19 (*(volatile uint8_t  *)0x41004453UL)
#define PORTA_PINCFG22 (*(volatile uint8_t  *)0x41004456UL)
#define PORTA_PINCFG23 (*(volatile uint8_t  *)0x41004457UL)

/* SERCOM5 USART (EDBG serial) */
#define SERCOM5_CTRLA    (*(volatile uint32_t *)0x42001C00UL)
#define SERCOM5_CTRLB    (*(volatile uint32_t *)0x42001C04UL)
#define SERCOM5_BAUD     (*(volatile uint16_t *)0x42001C0CUL)
#define SERCOM5_INTFLAG  (*(volatile uint8_t  *)0x42001C18UL)
#define SERCOM5_SYNCBUSY (*(volatile uint32_t *)0x42001C1CUL)
#define SERCOM5_DATA     (*(volatile uint16_t *)0x42001C28UL)
#define DRE_FLAG (1u << 0)

static void sercom5_init(void)
{
    PM_APBCMASK |= (1u << 7);
    GCLK_CLKCTRL = (uint16_t)(25u | (0u << 8) | (1u << 14));
    while (GCLK_STATUS & (1u << 7)) {}
    PORTA_PMUX11   = (3u << 4) | 3u;
    PORTA_PINCFG22 = (1u << 0);
    PORTA_PINCFG23 = (1u << 0) | (1u << 1);
    SERCOM5_CTRLA = (1u << 0);
    while (SERCOM5_SYNCBUSY & (1u << 0)) {}
    SERCOM5_CTRLA = (0x4u) | (1u << 20) | (1u << 28);
    SERCOM5_CTRLB = (1u << 16) | (1u << 17);
    while (SERCOM5_SYNCBUSY & (1u << 2)) {}
    SERCOM5_BAUD = 63019u;
    SERCOM5_CTRLA |= (1u << 1);
    while (SERCOM5_SYNCBUSY & (1u << 1)) {}
}

static void uart_putc(char c) { while (!(SERCOM5_INTFLAG & DRE_FLAG)) {} SERCOM5_DATA = (uint8_t)c; }
static void uart_puts(const char *s) { while (*s) { uart_putc(*s++); } }

static void uart_put_hex2(uint8_t b)
{
    const char hex[] = "0123456789ABCDEF";
    uart_putc(hex[b >> 4]); uart_putc(hex[b & 0xFu]);
}

static void uart_putu32(uint32_t n)
{
    char buf[10]; uint8_t i = 0;
    if (!n) { uart_putc('0'); return; }
    while (n) { buf[i++] = (char)('0' + n % 10u); n /= 10u; }
    while (i--) { uart_putc(buf[i]); }
}

/* =========================================================================
 * SERCOM1 SPI master — MOSI=PA16/PAD[0], SCK=PA17/PAD[1], MISO=PA19/PAD[3]
 * All peripheral C (mux=2).  CTRLB.LOOPBACK=1 for self-test.
 * GCLK ID 20 (SERCOM1_CORE), APBCMASK bit 3.
 * BAUD = fGCLK/(2*fSPI) - 1 = 48000000/(2000000) - 1 = 23 → 1 MHz.
 * ========================================================================= */
#define SERCOM1_CTRLA    (*(volatile uint32_t *)0x42000C00UL)
#define SERCOM1_CTRLB    (*(volatile uint32_t *)0x42000C04UL)
#define SERCOM1_BAUD     (*(volatile uint8_t  *)0x42000C0CUL)
#define SERCOM1_INTFLAG  (*(volatile uint8_t  *)0x42000C18UL)
#define SERCOM1_SYNCBUSY (*(volatile uint32_t *)0x42000C1CUL)
#define SERCOM1_DATA     (*(volatile uint32_t *)0x42000C28UL)

#define SPI_DRE  (1u << 0)
#define SPI_RXC  (1u << 2)
#define SPI_TXC  (1u << 1)

static void spi_init(void)
{
    PM_APBCMASK |= (1u << 3);  /* SERCOM1 */
    GCLK_CLKCTRL = (uint16_t)(20u | (0u << 8) | (1u << 14));
    while (GCLK_STATUS & (1u << 7)) {}

    /* PA16/PA17 → peripheral C */
    PORTA_PMUX8    = (2u << 4) | 2u;  /* PA16 even=C, PA17 odd=C */
    PORTA_PINCFG16 = (1u << 0);
    PORTA_PINCFG17 = (1u << 0);
    /* PA19 → peripheral C (MISO, PAD[3]) */
    PORTA_PMUX9    = PORTA_PMUX9 | (2u << 4); /* PA19 odd nibble = C */
    PORTA_PINCFG19 = (1u << 0) | (1u << 1);   /* PMUXEN | INEN */

    /* Reset SERCOM1 */
    SERCOM1_CTRLA = (1u << 0);
    while (SERCOM1_SYNCBUSY & (1u << 0)) {}

    /* SPI master: MODE=3→bits[4:2]=011=0xC, DOPO=0(MOSI/PAD0,SCK/PAD1),
       DIPO=3(MISO/PAD3)→bits[11:8]=3, DORD=0(MSB first) */
    SERCOM1_CTRLA = (3u << 2) | (3u << 8);  /* MODE=SPI_MASTER | DIPO=3 */
    while (SERCOM1_SYNCBUSY) {}

    /* CTRLB: RXEN=1, LOOPBACK=1, CHSIZE=8-bit */
    SERCOM1_CTRLB = (1u << 17) | (1u << 14);  /* RXEN | LOOPBACK */
    while (SERCOM1_SYNCBUSY) {}

    SERCOM1_BAUD = 23u;  /* 1 MHz */

    SERCOM1_CTRLA |= (1u << 1);  /* ENABLE */
    while (SERCOM1_SYNCBUSY & (1u << 1)) {}
}

static uint8_t spi_transfer(uint8_t b)
{
    while (!(SERCOM1_INTFLAG & SPI_DRE)) {}
    SERCOM1_DATA = b;
    while (!(SERCOM1_INTFLAG & SPI_RXC)) {}
    return (uint8_t)(SERCOM1_DATA & 0xFFu);
}

/* TC3 tick */
void TC3_Handler(void) { *(volatile uint8_t *)0x42002C0EUL = 1u; ktos_timer_irq_handler(); }

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
        uart_puts("  Arduino Zero (SAMD21G18)\r\n");
        uart_puts("  SERCOM1, 1 MHz, LOOPBACK bit\r\n");
        uart_puts("  (no external wiring needed)\r\n");
        uart_puts("=============================\r\n");
        return TEST_PERIOD_MS;
    }
    uint32_t errors = 0;
    for (uint16_t i = 0; i <= 0xFFu; ++i) {
        uint8_t sent = (uint8_t)i;
        uint8_t recv = spi_transfer(sent);
        if (recv != sent) {
            uart_puts("  ERR sent=0x"); uart_put_hex2(sent);
            uart_puts(" got=0x"); uart_put_hex2(recv); uart_puts("\r\n");
            ++errors;
        }
    }
    uart_puts("SPI loopback 0x00-0xFF: ");
    if (!errors) { uart_puts("PASS\r\n"); }
    else { uart_putu32(errors); uart_puts(" error(s) — FAIL\r\n"); }
    return TEST_PERIOD_MS;
}

int main(void)
{
    sercom5_init();
    ktos_InitTask(spi_task, 128, 4, 'S');
    ktos_RunOS();
    return 0;
}
