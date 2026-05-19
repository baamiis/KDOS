/*
 * KTOS — STM32F103 Blue Pill I2C scanner example (bare-metal)
 *
 * Single KTOS task scans I2C1 (100 kHz) for devices at 0x01–0x7E every 5 s.
 * Prints found addresses over USART1.
 *
 * I2C1 pins: PB6=SCL, PB7=SDA (open-drain AF, external 4.7 kΩ pull-ups).
 * APB1 clock: 36 MHz.  CCR = 36000000/(2*100000) = 180.  TRISE = 37.
 *
 * USART1: PA9=TX, 115200 8N1, 72 MHz APB2.
 */

#include <stdint.h>
#include <stdbool.h>
#include "../../../../core/ktos.h"

/* =========================================================================
 * RCC / GPIO / USART1
 * ========================================================================= */
#define RCC_APB1ENR  (*(volatile uint32_t *)0x4002101CUL)
#define RCC_APB2ENR  (*(volatile uint32_t *)0x40021018UL)

#define GPIOA_CRH    (*(volatile uint32_t *)0x40010804UL)
#define GPIOB_CRL    (*(volatile uint32_t *)0x40010C00UL)

#define USART1_SR    (*(volatile uint32_t *)0x40013800UL)
#define USART1_DR    (*(volatile uint32_t *)0x40013804UL)
#define USART1_BRR   (*(volatile uint32_t *)0x40013808UL)
#define USART1_CR1   (*(volatile uint32_t *)0x4001380CUL)
#define SR_TXE  (1u << 7)

/* =========================================================================
 * I2C1 registers (base 0x40005400)
 * ========================================================================= */
#define I2C1_CR1     (*(volatile uint32_t *)0x40005400UL)
#define I2C1_CR2     (*(volatile uint32_t *)0x40005404UL)
#define I2C1_DR      (*(volatile uint32_t *)0x40005410UL)
#define I2C1_SR1     (*(volatile uint32_t *)0x40005414UL)
#define I2C1_SR2     (*(volatile uint32_t *)0x40005418UL)
#define I2C1_CCR     (*(volatile uint32_t *)0x4000541CUL)
#define I2C1_TRISE   (*(volatile uint32_t *)0x40005420UL)

#define I2C_PE    (1u << 0)
#define I2C_START (1u << 8)
#define I2C_STOP  (1u << 9)
#define I2C_SWRST (1u << 15)
#define I2C_SB    (1u << 0)
#define I2C_ADDR  (1u << 1)
#define I2C_BTF   (1u << 2)
#define I2C_TXE   (1u << 7)
#define I2C_AF    (1u << 10)
#define I2C_BUSY  (1u << 1)

static void hw_init(void)
{
    RCC_APB2ENR |= (1u << 2) | (1u << 3) | (1u << 14);  /* GPIOA + GPIOB + USART1 */
    RCC_APB1ENR |= (1u << 21);                            /* I2C1 */

    /* PA9=TX AF PP 50 MHz */
    GPIOA_CRH = (GPIOA_CRH & ~(0xFu << 4)) | (0xBu << 4);

    /* PB6=SCL, PB7=SDA: AF open-drain 50 MHz (0xF each) */
    GPIOB_CRL = (GPIOB_CRL & ~(0xFFu << 24))
              | (0xFu << 24)   /* PB6 */
              | (0xFu << 28);  /* PB7 */

    USART1_BRR = 625u;
    USART1_CR1 = (1u << 13) | (1u << 3);  /* UE | TE */

    /* I2C1 init */
    I2C1_CR1 = I2C_SWRST;
    I2C1_CR1 = 0;
    I2C1_CR2   = 36u;   /* FREQ = 36 MHz (APB1) */
    I2C1_CCR   = 180u;  /* 100 kHz standard mode */
    I2C1_TRISE = 37u;
    I2C1_CR1   = I2C_PE;
}

static void uart_putc(char c) { while (!(USART1_SR & SR_TXE)) {} USART1_DR = (uint8_t)c; }
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

/* =========================================================================
 * I2C probe: returns true if 7-bit addr ACKs
 * ========================================================================= */
static bool i2c_probe(uint8_t addr)
{
    /* Wait until bus is idle */
    while (I2C1_SR2 & I2C_BUSY) {}

    /* Generate START */
    I2C1_CR1 |= I2C_START;
    while (!(I2C1_SR1 & I2C_SB)) {}

    /* Send address (write direction to probe) */
    I2C1_DR = (uint8_t)(addr << 1);  /* write bit = 0 */

    /* Wait for ADDR (ACK) or AF (NACK) */
    while (!(I2C1_SR1 & (I2C_ADDR | I2C_AF))) {}

    if (I2C1_SR1 & I2C_AF) {
        I2C1_SR1 &= ~I2C_AF;
        I2C1_CR1 |= I2C_STOP;
        return false;
    }

    /* Clear ADDR by reading SR1 then SR2 */
    (void)I2C1_SR1;
    (void)I2C1_SR2;

    I2C1_CR1 |= I2C_STOP;
    return true;
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

static WORD scanner_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam; (void)lParam;
    if (MsgType == KTOS_MSG_TYPE_INIT) {
        uart_puts("=============================\r\n");
        uart_puts("  KTOS I2C Scanner\r\n");
        uart_puts("  Blue Pill STM32F103\r\n");
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
