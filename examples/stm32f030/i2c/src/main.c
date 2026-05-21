/*
 * KTOS — STM32F030 Nucleo-F030R8 I2C scanner example (bare-metal)
 *
 * Single KTOS task scans I2C1 (100 kHz) for devices at 0x01–0x7E every 5 s.
 * Results printed over USART2 (ST-Link virtual COM).
 *
 * I2C1 pins: PB6=SCL, PB7=SDA, AF1, open-drain.  Add 4.7 kΩ pull-ups.
 * APB clock = 48 MHz.
 * TIMINGR = 0xB0420F13 (AN4235 Table 9: 48 MHz, 100 kHz, analog filter on).
 *
 * STM32F030 I2C uses new-style registers (TIMINGR, CR2 with AUTOEND/START).
 */

#include <stdint.h>
#include <stdbool.h>
#include "../../../../core/ktos.h"

#define RCC_AHBENR   (*(volatile uint32_t *)0x40021014UL)
#define RCC_APB1ENR  (*(volatile uint32_t *)0x4002101CUL)

#define GPIOA_MODER  (*(volatile uint32_t *)0x48000000UL)
#define GPIOA_AFRL   (*(volatile uint32_t *)0x48000020UL)

#define GPIOB_MODER  (*(volatile uint32_t *)0x48000400UL)
#define GPIOB_OTYPER (*(volatile uint32_t *)0x48000404UL)
#define GPIOB_AFRL   (*(volatile uint32_t *)0x48000420UL)

#define USART2_CR1   (*(volatile uint32_t *)0x40004400UL)
#define USART2_BRR   (*(volatile uint32_t *)0x4000440CUL)
#define USART2_ISR   (*(volatile uint32_t *)0x4000441CUL)
#define USART2_TDR   (*(volatile uint32_t *)0x40004428UL)
#define ISR_TXE  (1u << 7)

/* I2C1 new-style registers (base 0x40005400) */
#define I2C1_CR1     (*(volatile uint32_t *)0x40005400UL)
#define I2C1_CR2     (*(volatile uint32_t *)0x40005404UL)
#define I2C1_TIMINGR (*(volatile uint32_t *)0x40005410UL)
#define I2C1_ISR     (*(volatile uint32_t *)0x40005418UL)
#define I2C1_ICR     (*(volatile uint32_t *)0x4000541CUL)

#define I2C_PE      (1u << 0)
#define I2C_START   (1u << 13)
#define I2C_AUTOEND (1u << 25)
#define I2C_NACKF   (1u << 4)
#define I2C_STOPF   (1u << 5)
#define I2C_BUSY    (1u << 15)

static void hw_init(void)
{
    RCC_AHBENR  |= (1u << 17) | (1u << 18);  /* GPIOA + GPIOB */
    RCC_APB1ENR |= (1u << 17) | (1u << 21);  /* USART2 + I2C1 */

    /* PA2=TX AF1 */
    GPIOA_MODER = (GPIOA_MODER & ~(3u << 4)) | (2u << 4);
    GPIOA_AFRL  = (GPIOA_AFRL  & ~(0xFu << 8)) | (1u << 8);

    /* PB6=SCL, PB7=SDA: AF mode, open-drain, AF1 */
    GPIOB_MODER = (GPIOB_MODER & ~(0xFFu << 12))
                | (2u << 12)   /* PB6 AF */
                | (2u << 14);  /* PB7 AF */
    GPIOB_OTYPER |= (1u << 6) | (1u << 7);  /* open-drain */
    GPIOB_AFRL = (GPIOB_AFRL & ~(0xFFu << 24))
               | (1u << 24)   /* PB6 AF1 */
               | (1u << 28);  /* PB7 AF1 */

    USART2_BRR = 417u;
    USART2_CR1 = (1u << 0) | (1u << 3);  /* UE | TE */

    /* I2C1: 100 kHz at 48 MHz (AN4235 Table 9, analog filter on) */
    I2C1_CR1     = 0;           /* disable while configuring */
    I2C1_TIMINGR = 0xB0420F13UL;
    I2C1_CR1     = I2C_PE;
}

static void uart_putc(char c) { while (!(USART2_ISR & ISR_TXE)) {} USART2_TDR = (uint8_t)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void uart_put_hex2(uint8_t b)
{
    const char h[] = "0123456789ABCDEF";
    uart_putc(h[b >> 4]); uart_putc(h[b & 0xFu]);
}
static void uart_putu8(uint8_t n)
{
    char buf[4]; uint8_t i = 0;
    if (!n) { uart_putc('0'); return; }
    while (n) { buf[i++] = (char)('0' + n % 10u); n = (uint8_t)(n / 10u); }
    while (i--) uart_putc(buf[i]);
}

void ktos_timer_irq_handler(void);
void SysTick_Handler(void) { ktos_timer_irq_handler(); }
__attribute__((noreturn)) void ktos_Emergency(const char *msg)
{ uart_puts("\r\n[KTOS FATAL] "); uart_puts(msg); while (1) {} }
void ktos_DebugPrintf(const char *f, ...) { (void)f; }
void ktos_InitSys(void) {}

static bool i2c_probe(uint8_t addr)
{
    /* Wait bus idle */
    while (I2C1_ISR & I2C_BUSY) {}

    /* Clear flags then start: SADD=addr<<1, NBYTES=0, AUTOEND, START */
    I2C1_ICR = I2C_NACKF | I2C_STOPF;
    I2C1_CR2 = ((uint32_t)addr << 1) | I2C_AUTOEND | I2C_START;

    /* Wait for NACKF (no device) or STOPF (device ACKed) */
    uint32_t isr;
    do { isr = I2C1_ISR; } while (!((isr & I2C_NACKF) || (isr & I2C_STOPF)));

    bool found = !(I2C1_ISR & I2C_NACKF);
    I2C1_ICR = I2C_NACKF | I2C_STOPF;
    return found;
}

static void run_scan(void)
{
    uart_puts("Scanning I2C bus (0x01–0x7E)...\r\n");
    uint8_t found = 0;
    for (uint8_t addr = 0x01u; addr <= 0x7Eu; ++addr) {
        if (i2c_probe(addr)) {
            uart_puts("  Found: 0x"); uart_put_hex2(addr); uart_puts("\r\n");
            ++found;
        }
    }
    if (!found) { uart_puts("No I2C devices found.\r\n"); }
    else { uart_putu8(found); uart_puts(" device(s) found.\r\n"); }
    uart_puts("\r\n");
}

#define SCAN_PERIOD_MS 5000U

static WORD scanner_task(WORD MsgType, WORD Param1, LONG Param2)
{
    (void)Param1; (void)Param2;
    if (MsgType == KTOS_MSG_TYPE_INIT) {
        uart_puts("=============================\r\n");
        uart_puts("  KTOS I2C Scanner\r\n");
        uart_puts("  Nucleo-F030R8\r\n");
        uart_puts("  PB6=SCL  PB7=SDA  100 kHz\r\n");
        uart_puts("  4.7k pull-ups required\r\n");
        uart_puts("=============================\r\n");
        run_scan();
        return SCAN_PERIOD_MS;
    }
    run_scan();
    return SCAN_PERIOD_MS;
}

int main(void)
{
    hw_init();
    ktos_InitTask(scanner_task, 512, 4, 'I');
    ktos_RunOS();
    return 0;
}
