/*
 * KTOS — STM32F030 Nucleo-F030R8 SPI loopback example (bare-metal)
 *
 * Single KTOS task exercises SPI1 master in external loopback mode.
 * Sends bytes 0x00–0xFF every 2 s, prints PASS / fail count.
 *
 * SPI1 pins (AF0 on GPIOA):
 *   PA5 = SCK   (also LD2 LED — LED inactive while SPI runs)
 *   PA6 = MISO
 *   PA7 = MOSI
 *
 * Loopback: bridge PA6 (MISO) to PA7 (MOSI) with a jumper wire.
 * STM32F030 SPI has no hardware loopback bit.
 *
 * SPI clock: APB (48 MHz) / 8 → 6 MHz (BR=010).
 * USART2: PA2=TX, 115200 8N1 → ST-Link virtual COM.
 */

#include <stdint.h>
#include "../../../../core/ktos.h"

#define RCC_AHBENR   (*(volatile uint32_t *)0x40021014UL)
#define RCC_APB1ENR  (*(volatile uint32_t *)0x4002101CUL)
#define RCC_APB2ENR  (*(volatile uint32_t *)0x40021018UL)

#define GPIOA_MODER  (*(volatile uint32_t *)0x48000000UL)
#define GPIOA_AFRL   (*(volatile uint32_t *)0x48000020UL)

#define USART2_CR1   (*(volatile uint32_t *)0x40004400UL)
#define USART2_BRR   (*(volatile uint32_t *)0x4000440CUL)
#define USART2_ISR   (*(volatile uint32_t *)0x4000441CUL)
#define USART2_TDR   (*(volatile uint32_t *)0x40004428UL)
#define ISR_TXE  (1u << 7)

#define SPI1_CR1     (*(volatile uint32_t *)0x40013000UL)
#define SPI1_SR      (*(volatile uint32_t *)0x40013008UL)
#define SPI1_DR_BYTE (*(volatile uint8_t  *)0x4001300CUL)
#define SPI_TXE  (1u << 1)
#define SPI_RXNE (1u << 0)
#define SPI_BSY  (1u << 7)

static void hw_init(void)
{
    RCC_AHBENR  |= (1u << 17);  /* GPIOA */
    RCC_APB1ENR |= (1u << 17);  /* USART2 */
    RCC_APB2ENR |= (1u << 12);  /* SPI1 */

    /* PA2=TX AF1, PA5=SCK AF0, PA6=MISO input(AF0), PA7=MOSI AF0 */
    GPIOA_MODER = (GPIOA_MODER & ~(0x3FFFu << 4))
                | (2u << 4)    /* PA2  AF  */
                | (2u << 10)   /* PA5  AF  */
                | (2u << 12)   /* PA6  AF  */
                | (2u << 14);  /* PA7  AF  */

    /* AFRL: PA2=AF1, PA5/PA6/PA7=AF0 (already 0) */
    GPIOA_AFRL = (GPIOA_AFRL & ~(0xFu << 8)) | (1u << 8);  /* PA2 AF1 */

    USART2_BRR = 417u;
    USART2_CR1 = (1u << 0) | (1u << 3);  /* UE | TE */

    /* SPI1 master: BR=010(÷8=6MHz), CPOL=0, CPHA=0, SSM+SSI=1, SPE=1 */
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

static void uart_putc(char c) { while (!(USART2_ISR & ISR_TXE)) {} USART2_TDR = (uint8_t)c; }
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
        uart_puts("  Nucleo-F030R8\r\n");
        uart_puts("  SPI1 @ 6 MHz\r\n");
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
            uart_puts(" got=0x"); uart_put_hex2(recv); uart_puts("\r\n");
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
