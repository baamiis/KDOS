/*
 * KTOS — STM32F030 Nucleo-F030R8 UART example (bare-metal)
 *
 * Two cooperating KTOS tasks form a tiny shell over USART2:
 *
 *   rx_task ('R') — polls every 10 ms, drains USART2 RX, echoes bytes,
 *                   assembles lines, posts MSG_LINE_READY to cmd_task.
 *   cmd_task ('C') — waits MSG_LINE_READY, dispatches PING/INFO/HELP.
 *
 * USART2 is connected to the ST-Link virtual COM port on Nucleo:
 *   PA2 = TX (AF1), PA3 = RX (AF1), 115200 8N1, 48 MHz APB.
 *   BRR = 48000000/115200 = 417 (mantissa=26, fraction=1).
 *
 * Tick: SysTick → 1 ms at 48 MHz (LOAD = 47999).
 * Clock: HSI 8 MHz → HSI/2 (4 MHz) → PLL ×12 = 48 MHz (set by startup.c).
 *
 * No external hardware required — just the Nucleo and a USB cable.
 */

#include <stdint.h>
#include <string.h>
#include "../../../../core/ktos.h"

/* =========================================================================
 * RCC / GPIO (STM32F0: GPIO clocks in AHBENR, GPIO base 0x4800xxxx)
 * USART2 (STM32F0 new register map: ISR, TDR, RDR)
 * ========================================================================= */
#define RCC_AHBENR   (*(volatile uint32_t *)0x40021014UL)
#define RCC_APB1ENR  (*(volatile uint32_t *)0x4002101CUL)

#define GPIOA_MODER  (*(volatile uint32_t *)0x48000000UL)
#define GPIOA_AFRL   (*(volatile uint32_t *)0x48000020UL)

#define USART2_CR1   (*(volatile uint32_t *)0x40004400UL)
#define USART2_BRR   (*(volatile uint32_t *)0x4000440CUL)
#define USART2_ISR   (*(volatile uint32_t *)0x4000441CUL)
#define USART2_RDR   (*(volatile uint32_t *)0x40004424UL)
#define USART2_TDR   (*(volatile uint32_t *)0x40004428UL)

#define ISR_TXE  (1u << 7)
#define ISR_RXNE (1u << 5)

static void usart2_init(void)
{
    RCC_AHBENR  |= (1u << 17);  /* GPIOA clock */
    RCC_APB1ENR |= (1u << 17);  /* USART2 clock */

    /* PA2=TX, PA3=RX → AF1: MODER bits[5:4]=10, bits[7:6]=10 */
    GPIOA_MODER = (GPIOA_MODER & ~(0xFFu << 4))
                | (2u << 4)    /* PA2 AF */
                | (2u << 6);   /* PA3 AF */

    /* AFRL: PA2 bits[11:8]=0001, PA3 bits[15:12]=0001 */
    GPIOA_AFRL = (GPIOA_AFRL & ~(0xFFu << 8))
               | (1u << 8)    /* PA2 AF1 */
               | (1u << 12);  /* PA3 AF1 */

    USART2_BRR = 417u;                          /* 115200 at 48 MHz */
    USART2_CR1 = (1u << 0) | (1u << 3) | (1u << 2);  /* UE | TE | RE */
}

static void uart_putc(char c)
{
    while (!(USART2_ISR & ISR_TXE)) {}
    USART2_TDR = (uint8_t)c;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static int16_t uart_getc(void)
{
    if (!(USART2_ISR & ISR_RXNE)) { return -1; }
    return (int16_t)(USART2_RDR & 0xFFu);
}

void ktos_timer_irq_handler(void);
void SysTick_Handler(void) { ktos_timer_irq_handler(); }
__attribute__((noreturn)) void ktos_Emergency(const char *msg)
{ uart_puts("\r\n[KTOS FATAL] "); uart_puts(msg); while (1) {} }
void ktos_DebugPrintf(const char *f, ...) { (void)f; }
void ktos_InitSys(void) {}

/* =========================================================================
 * Tasks
 * ========================================================================= */
#define MSG_LINE_READY  ((WORD)(KTOS_MSG_TYPE_SYSTEM_START + 1))
#define LINE_BUF_SIZE   64U
#define RX_POLL_MS      10U

static char    g_line[LINE_BUF_SIZE];
static uint8_t g_line_len;
static struct ktos_TASK *g_cmd_task;

static WORD cmd_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam; (void)lParam;
    if (MsgType == KTOS_MSG_TYPE_INIT) {
        uart_puts("=============================\r\n");
        uart_puts("  KTOS UART — Nucleo-F030R8\r\n");
        uart_puts("  USART2  115200 8N1\r\n");
        uart_puts("  Type HELP for commands.\r\n");
        uart_puts("=============================\r\n");
        return KTOS_MSG_SLEEP_INDEFINITLY;
    }
    if (MsgType != MSG_LINE_READY) { return KTOS_MSG_SLEEP_INDEFINITLY; }
    if (strcmp(g_line, "PING") == 0)      { uart_puts("PONG\r\n"); }
    else if (strcmp(g_line, "INFO") == 0) {
        uart_puts("Board : STM32F030R8 Nucleo\r\n");
        uart_puts("Core  : ARM Cortex-M0 @ 48 MHz\r\n");
        uart_puts("RAM   : 8 KB   Flash: 64 KB\r\n");
        uart_puts("OS    : KTOS (cooperative)\r\n");
        uart_puts("Tick  : SysTick 1 ms\r\n");
    }
    else if (strcmp(g_line, "HELP") == 0) { uart_puts("Commands: PING  INFO  HELP\r\n"); }
    else if (g_line[0] != '\0')           { uart_puts("Unknown. Type HELP.\r\n"); }
    return KTOS_MSG_SLEEP_INDEFINITLY;
}

static WORD rx_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam; (void)lParam;
    if (MsgType == KTOS_MSG_TYPE_INIT) { g_line_len = 0; return RX_POLL_MS; }
    int16_t b;
    while ((b = uart_getc()) >= 0) {
        char c = (char)b;
        uart_putc(c);
        if (c == '\r') { uart_putc('\n'); }
        if (c == '\r' || c == '\n') {
            if (g_line_len > 0) {
                g_line[g_line_len] = '\0';
                ktos_SendMsg(g_cmd_task, MSG_LINE_READY, (WORD)g_line_len, 0);
                g_line_len = 0;
            }
            continue;
        }
        char upper = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
        if (g_line_len < (LINE_BUF_SIZE - 1)) { g_line[g_line_len++] = upper; }
    }
    return RX_POLL_MS;
}

int main(void)
{
    usart2_init();
    g_cmd_task = ktos_InitTask(cmd_task, 256, 4, 'C');
    ktos_InitTask(rx_task, 256, 4, 'R');
    ktos_RunOS();
    return 0;
}
