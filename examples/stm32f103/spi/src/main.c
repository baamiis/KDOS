/*
 * KTOS — STM32F103 Blue Pill SPI loopback example (bare-metal)
 *
 * Single KTOS task exercises SPI1 master in external loopback mode.
 * Sends bytes 0x00–0xFF every 2 s, prints PASS / fail count.
 *
 * SPI1 pins (all peripheral AF on GPIOA):
 *   PA5 = SCK  (SERCOM1 PAD[1])
 *   PA6 = MISO
 *   PA7 = MOSI
 *
 * Loopback: bridge PA6 (MISO) to PA7 (MOSI) with a jumper wire.
 *
 * Clock: 72 MHz APB2, BR=010 → ÷8 → 9 MHz SPI clock.
 * USART1: PA9=TX, 115200 8N1.
 *
 * NOTE: STM32F103 SPI has no hardware loopback bit.
 *       A PA6–PA7 jumper wire is required.
 */

#include <stdint.h>
#include "../../../../core/ktos.h"

/* =========================================================================
 * RCC / GPIO / USART1 / SPI1
 * ========================================================================= */
#define RCC_APB2ENR  (*(volatile uint32_t *)0x40021018UL)

#define GPIOA_CRL    (*(volatile uint32_t *)0x40010800UL)
#define GPIOA_CRH    (*(volatile uint32_t *)0x40010804UL)

#define USART1_SR    (*(volatile uint32_t *)0x40013800UL)
#define USART1_DR    (*(volatile uint32_t *)0x40013804UL)
#define USART1_BRR   (*(volatile uint32_t *)0x40013808UL)
#define USART1_CR1   (*(volatile uint32_t *)0x4001380CUL)
#define SR_TXE  (1u << 7)

#define SPI1_CR1     (*(volatile uint32_t *)0x40013000UL)
#define SPI1_SR      (*(volatile uint32_t *)0x40013008UL)
/* Byte-wide access to SPI1_DR avoids packing 16-bit frames */
#define SPI1_DR_BYTE (*(volatile uint8_t  *)0x4001300CUL)

#define SPI_TXE  (1u << 1)
#define SPI_RXNE (1u << 0)
#define SPI_BSY  (1u << 7)

static void hw_init(void)
{
    RCC_APB2ENR |= (1u << 2) | (1u << 12) | (1u << 14); /* GPIOA + SPI1 + USART1 */

    /* PA5=SCK AF PP 50 MHz, PA6=MISO input floating, PA7=MOSI AF PP 50 MHz */
    GPIOA_CRL = (GPIOA_CRL & ~(0xFFFu << 20))
              | (0xBu << 20)   /* PA5 SCK  */
              | (0x4u << 24)   /* PA6 MISO */
              | (0xBu << 28);  /* PA7 MOSI */

    /* PA9=TX AF PP 50 MHz */
    GPIOA_CRH = (GPIOA_CRH & ~(0xFu << 4)) | (0xBu << 4);

    USART1_BRR = 625u;
    USART1_CR1 = (1u << 13) | (1u << 3);  /* UE | TE */

    /* SPI1 master: BR=010(÷8=9MHz), CPOL=0, CPHA=0, SSM+SSI=1, SPE=1 */
    SPI1_CR1 = (1u << 2)   /* MSTR */
             | (2u << 3)   /* BR = /8 */
             | (1u << 9)   /* SSM */
             | (1u << 8)   /* SSI */
             | (1u << 6);  /* SPE */
}

static uint8_t spi_transfer(uint8_t b)
{
    while (!(SPI1_SR & SPI_TXE)) {}
    SPI1_DR_BYTE = b;
    while (!(SPI1_SR & SPI_RXNE)) {}
    return SPI1_DR_BYTE;
}

static void uart_putc(char c) { while (!(USART1_SR & SR_TXE)) {} USART1_DR = (uint8_t)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void uart_put_hex2(uint8_t b)
{
    const char h[] = "0123456789ABCDEF";
    uart_putc(h[b >> 4]); uart_putc(h[b & 0xFu]);
}
static void uart_putu32(uint32_t n)
{
    char buf[10]; uint8_t i = 0;
    if (!n) { uart_putc('0'); return; }
    while (n) { buf[i++] = (char)('0' + n % 10u); n /= 10u; }
    while (i--) uart_putc(buf[i]);
}

void ktos_timer_irq_handler(void);
void SysTick_Handler(void) { ktos_timer_irq_handler(); }
__attribute__((noreturn)) void ktos_Emergency(const char *msg)
{ uart_puts("\r\n[KTOS FATAL] "); uart_puts(msg); while (1) {} }
void ktos_DebugPrintf(const char *f, ...) { (void)f; }
void ktos_InitSys(void) {}

#define TEST_PERIOD_MS 2000U

static WORD spi_task(WORD MsgType, WORD Param1, LONG Param2)
{
    (void)Param1; (void)Param2;
    if (MsgType == KTOS_MSG_TYPE_INIT) {
        uart_puts("=============================\r\n");
        uart_puts("  KTOS SPI Loopback\r\n");
        uart_puts("  Blue Pill STM32F103\r\n");
        uart_puts("  SPI1 @ 9 MHz\r\n");
        uart_puts("  Bridge PA6(MISO)–PA7(MOSI)\r\n");
        uart_puts("=============================\r\n");
        return TEST_PERIOD_MS;
    }
    uint32_t errors = 0;
    for (uint16_t i = 0; i <= 0xFFu; ++i) {
        uint8_t sent = (uint8_t)i;
        uint8_t recv = spi_transfer(sent);
        if (recv != sent) {
            uart_puts("  ERR sent=0x"); uart_put_hex2(sent);
            uart_puts(" got=0x");  uart_put_hex2(recv); uart_puts("\r\n");
            ++errors;
        }
    }
    uart_puts("SPI 0x00–0xFF: ");
    if (!errors) { uart_puts("PASS\r\n"); }
    else { uart_putu32(errors); uart_puts(" error(s) — FAIL\r\n"); }
    return TEST_PERIOD_MS;
}

int main(void)
{
    hw_init();
    ktos_InitTask(spi_task, 256, 4, 'S');
    ktos_RunOS();
    return 0;
}
