/*
 * KTOS — Arduino Zero I2C scanner example (bare-metal)
 *
 * Single KTOS task scans SERCOM3 I2C master (100 kHz) from 0x01 to 0x7E
 * every 5 s.  A device ACKs the read probe if present; prints its address.
 *
 * I2C pins: SDA = PA22 (D20), SCL = PA23 (D21), SERCOM3 PAD[0]/PAD[1],
 * peripheral function C (mux value 2).
 *
 * Serial output uses SERCOM0 USART on PA10 (TX/D1) and PA11 (RX/D0)
 * via peripheral function C.  Connect a 3.3 V USB-to-serial adapter:
 *   adapter RX → D1 (PA10)  adapter TX → D0 (PA11)
 *
 * NOTE: PA22/PA23 are shared with SERCOM5 (EDBG UART, peripheral D).
 * Using SERCOM3 (peripheral C) on the same pins is safe when SERCOM5
 * is not enabled — they use different mux settings.
 *
 * Clock: 48 MHz.  BAUD for SERCOM0 at 115200: 63019.
 * SERCOM3 BAUD for I2C: BAUD = fGCLK/(2*fSCL) - 5 = 48MHz/(2*100kHz) - 5 = 235.
 */

#include <stdint.h>
#include <stdbool.h>
#include "../../../../../core/ktos.h"

/* PM / GCLK */
#define PM_APBCMASK  (*(volatile uint32_t *)0x40000420UL)
#define GCLK_CLKCTRL (*(volatile uint16_t *)0x40000C02UL)
#define GCLK_STATUS  (*(volatile uint8_t  *)0x40000C01UL)

/* PORT A */
#define PORTA_PMUX5    (*(volatile uint8_t  *)0x41004435UL) /* PA10/PA11 */
#define PORTA_PMUX11   (*(volatile uint8_t  *)0x4100443BUL) /* PA22/PA23 */
#define PORTA_PINCFG10 (*(volatile uint8_t  *)0x4100444AUL)
#define PORTA_PINCFG11 (*(volatile uint8_t  *)0x4100444BUL)
#define PORTA_PINCFG22 (*(volatile uint8_t  *)0x41004456UL)
#define PORTA_PINCFG23 (*(volatile uint8_t  *)0x41004457UL)

/* =========================================================================
 * SERCOM0 USART — TX=PA10 (D1), RX=PA11 (D0), peripheral C (mux=2)
 * GCLK peripheral ID 19 (SERCOM0_CORE), APBCMASK bit 2.
 * TXPO=1 → TX on PAD[2]=PA10.  RXPO=3 → RX on PAD[3]=PA11.
 * ========================================================================= */
#define SERCOM0_CTRLA    (*(volatile uint32_t *)0x42000800UL)
#define SERCOM0_CTRLB    (*(volatile uint32_t *)0x42000804UL)
#define SERCOM0_BAUD     (*(volatile uint16_t *)0x4200080CUL)
#define SERCOM0_INTFLAG  (*(volatile uint8_t  *)0x42000818UL)
#define SERCOM0_SYNCBUSY (*(volatile uint32_t *)0x4200081CUL)
#define SERCOM0_DATA     (*(volatile uint16_t *)0x42000828UL)
#define DRE_FLAG (1u << 0)

static void sercom0_uart_init(void)
{
    PM_APBCMASK |= (1u << 2);  /* SERCOM0 */
    GCLK_CLKCTRL = (uint16_t)(19u | (0u << 8) | (1u << 14)); /* ID=19|GEN=0|CLKEN */
    while (GCLK_STATUS & (1u << 7)) {}
    /* PA10=TX → SERCOM0/PAD[2], peripheral C (mux=2); PA11=RX → PAD[3], C */
    PORTA_PMUX5   = (2u << 4) | 2u;    /* PA10 even=C, PA11 odd=C */
    PORTA_PINCFG10 = (1u << 0);         /* PMUXEN */
    PORTA_PINCFG11 = (1u << 0) | (1u << 1); /* PMUXEN | INEN */
    SERCOM0_CTRLA = (1u << 0);          /* SWRST */
    while (SERCOM0_SYNCBUSY & (1u << 0)) {}
    /* TXPO=1 (TX on PAD[2]→bits[17:16]=01), RXPO=3 (RX on PAD[3]→bits[21:20]=11) */
    SERCOM0_CTRLA = (0x4u) | (1u << 16) | (3u << 20) | (1u << 28);
    SERCOM0_CTRLB = (1u << 16) | (1u << 17);
    while (SERCOM0_SYNCBUSY & (1u << 2)) {}
    SERCOM0_BAUD = 63019u;
    SERCOM0_CTRLA |= (1u << 1);
    while (SERCOM0_SYNCBUSY & (1u << 1)) {}
}

static void uart_putc(char c) { while (!(SERCOM0_INTFLAG & DRE_FLAG)) {} SERCOM0_DATA = (uint8_t)c; }
static void uart_puts(const char *s) { while (*s) { uart_putc(*s++); } }

static void uart_put_hex2(uint8_t b)
{
    const char hex[] = "0123456789ABCDEF";
    uart_putc(hex[b >> 4]); uart_putc(hex[b & 0xFu]);
}

static void uart_putu8(uint8_t n)
{
    char buf[4]; uint8_t i = 0;
    if (!n) { uart_putc('0'); return; }
    while (n) { buf[i++] = (char)('0' + n % 10u); n = (uint8_t)(n / 10u); }
    while (i--) { uart_putc(buf[i]); }
}

/* =========================================================================
 * SERCOM3 I2C master — SDA=PA22 (D20), SCL=PA23 (D21), peripheral C
 * GCLK ID 22 (SERCOM3_CORE), APBCMASK bit 5.
 * BAUD register = fGCLK/(2*fSCL) - 5 = 48000000/(200000) - 5 = 235.
 * ========================================================================= */
#define SERCOM3_CTRLA    (*(volatile uint32_t *)0x42001400UL)
#define SERCOM3_CTRLB    (*(volatile uint32_t *)0x42001404UL)
#define SERCOM3_BAUD     (*(volatile uint16_t *)0x4200140CUL)
#define SERCOM3_INTFLAG  (*(volatile uint8_t  *)0x42001418UL)
#define SERCOM3_STATUS   (*(volatile uint16_t *)0x4200141AUL)
#define SERCOM3_SYNCBUSY (*(volatile uint32_t *)0x4200141CUL)
#define SERCOM3_ADDR     (*(volatile uint32_t *)0x42001424UL)
#define SERCOM3_DATA     (*(volatile uint8_t  *)0x42001428UL)

/* INTFLAG bits */
#define I2C_MB   (1u << 0)  /* master on bus */
#define I2C_SB   (1u << 1)  /* slave on bus */
/* STATUS bits */
#define I2C_RXNACK (1u << 4)
#define I2C_BUSSTATE_MASK (3u << 0)
#define I2C_BUSSTATE_IDLE (1u << 0)
/* ADDR bits */
#define I2C_LENEN  (1u << 13)

static void i2c_init(void)
{
    PM_APBCMASK |= (1u << 5);  /* SERCOM3 */
    GCLK_CLKCTRL = (uint16_t)(22u | (0u << 8) | (1u << 14));
    while (GCLK_STATUS & (1u << 7)) {}

    /* PA22 and PA23 → SERCOM3 PAD[0]/PAD[1] via peripheral C (mux=2) */
    PORTA_PMUX11   = (2u << 4) | 2u;
    PORTA_PINCFG22 = (1u << 0) | (1u << 1); /* PMUXEN | INEN */
    PORTA_PINCFG23 = (1u << 0) | (1u << 1);

    /* Reset SERCOM3 */
    SERCOM3_CTRLA = (1u << 0);  /* SWRST */
    while (SERCOM3_SYNCBUSY & (1u << 0)) {}

    /* I2C Master mode (MODE=5 → bits[4:2]=101 = 0x14) */
    SERCOM3_CTRLA = (5u << 2) | (1u << 27); /* MODE=I2C_MASTER | SCLSM=0 | INACTOUT=0 */
    while (SERCOM3_SYNCBUSY & (1u << 2)) {}

    /* CTRLB: SMEN=0, QCEN=0 */
    SERCOM3_CTRLB = 0;
    while (SERCOM3_SYNCBUSY & (1u << 2)) {}

    /* BAUD: 235 for 100 kHz at 48 MHz */
    SERCOM3_BAUD = (uint16_t)(235u | (235u << 8)); /* BAUD | BAUDLOW */
    while (SERCOM3_SYNCBUSY) {}

    /* Enable */
    SERCOM3_CTRLA |= (1u << 1);
    while (SERCOM3_SYNCBUSY & (1u << 1)) {}

    /* Force bus state to IDLE */
    SERCOM3_STATUS = I2C_BUSSTATE_IDLE;
    while (SERCOM3_SYNCBUSY) {}
}

/* Probe 7-bit address: returns true if ACK received. */
static bool i2c_probe(uint8_t addr)
{
    /* Write address with READ bit set, then immediately NACK+STOP */
    SERCOM3_ADDR = ((uint32_t)addr << 1) | 1u;  /* addr<<1 | READ=1 */
    while (!(SERCOM3_INTFLAG & I2C_SB)) {}       /* wait slave-on-bus */

    if (SERCOM3_STATUS & I2C_RXNACK) {
        /* NACK: send stop, clear flag */
        SERCOM3_CTRLB |= (3u << 6);  /* CMD=3 → ACK+STOP */
        while (SERCOM3_SYNCBUSY) {}
        SERCOM3_INTFLAG = I2C_SB;
        return false;
    }

    /* ACK: issue NACK+STOP to terminate */
    SERCOM3_CTRLB = (1u << 6) | (3u << 6);  /* ACKACT=1(NACK) | CMD=3(STOP) */
    while (SERCOM3_SYNCBUSY) {}
    SERCOM3_INTFLAG = I2C_SB;
    return true;
}

/* TC3 tick */
void TC3_Handler(void) { *(volatile uint8_t *)0x42002C0EUL = 1u; ktos_timer_irq_handler(); }

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
        if (i2c_probe(addr)) {
            uart_puts("  Found: 0x"); uart_put_hex2(addr); uart_puts("\r\n");
            ++found;
        }
    }
    if (!found) { uart_puts("No I2C devices found.\r\n"); }
    else { uart_putu8(found); uart_puts(found == 1u ? " device found.\r\n" : " devices found.\r\n"); }
    uart_puts("\r\n");
}

static WORD scanner_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam; (void)lParam;
    if (MsgType == KTOS_MSG_TYPE_INIT) {
        i2c_init();
        uart_puts("=============================\r\n");
        uart_puts("  KTOS I2C Scanner\r\n");
        uart_puts("  Arduino Zero (SAMD21G18)\r\n");
        uart_puts("  SDA=D20(PA22)  SCL=D21(PA23)\r\n");
        uart_puts("  SERCOM3, 100 kHz\r\n");
        uart_puts("  Serial out: D1(TX)/D0(RX) @ 115200\r\n");
        uart_puts("=============================\r\n");
        run_scan();
        return SCAN_PERIOD_MS;
    }
    run_scan();
    return SCAN_PERIOD_MS;
}

int main(void)
{
    sercom0_uart_init();
    ktos_InitTask(scanner_task, 256, 4, 'I');
    ktos_RunOS();
    return 0;
}
