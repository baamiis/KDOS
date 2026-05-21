/*
 * KTOS — STM32F103 Blue Pill UART example (bare-metal)
 *
 * Two cooperating KTOS tasks form a tiny shell over USART1:
 *
 *   rx_task ('R') — wakes every 10 ms, drains USART1 RX, echoes each byte,
 *                   assembles a line, posts MSG_LINE_READY to cmd_task.
 *   cmd_task ('C') — waits MSG_LINE_READY, dispatches PING/INFO/HELP.
 *
 * USART1: PA9=TX, PA10=RX, 115200 8N1, 72 MHz APB2.
 * BRR = 72000000/115200 = 625 (mantissa=39, fraction=1).
 *
 * Tick: SysTick → 1 ms at 72 MHz (LOAD = 71999).
 * Clock: HSE 8 MHz → PLL ×9 = 72 MHz (set by startup.c).
 *
 * Wiring: connect a 3.3 V USB-serial adapter to PA9 (TX) and PA10 (RX).
 */

#include <stdint.h>
#include <string.h>
#include "../../../../core/ktos.h"

/* =========================================================================
 * RCC / GPIO / USART1 registers
 * ========================================================================= */
#define RCC_APB2ENR  (*(volatile uint32_t *)0x40021018UL)

#define GPIOA_CRH    (*(volatile uint32_t *)0x40010804UL)

#define USART1_SR    (*(volatile uint32_t *)0x40013800UL)
#define USART1_DR    (*(volatile uint32_t *)0x40013804UL)
#define USART1_BRR   (*(volatile uint32_t *)0x40013808UL)
#define USART1_CR1   (*(volatile uint32_t *)0x4001380CUL)

#define SR_TXE   (1u << 7)
#define SR_RXNE  (1u << 5)

static void usart1_init(void)
{
    RCC_APB2ENR |= (1u << 2) | (1u << 14);  /* GPIOA + USART1 */

    /* PA9=TX: AF push-pull 50 MHz (CRH bits[7:4]=0xB) */
    GPIOA_CRH = (GPIOA_CRH & ~(0xFFu << 4))
              | (0xBu << 4)    /* PA9  TX: AF PP 50 MHz */
              | (0x4u << 8);   /* PA10 RX: input floating */

    USART1_BRR = 625u;                          /* 115200 baud at 72 MHz */
    USART1_CR1 = (1u << 13) | (1u << 3) | (1u << 2); /* UE | TE | RE */
}

static void uart_putc(char c)
{
    while (!(USART1_SR & SR_TXE)) {}
    USART1_DR = (uint8_t)c;
}

static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

static int16_t uart_getc(void)
{
    if (!(USART1_SR & SR_RXNE)) { return -1; }
    return (int16_t)(USART1_DR & 0xFFu);
}

/* =========================================================================
 * SysTick ISR — feeds the KTOS 1 ms tick
 * ========================================================================= */
void ktos_timer_irq_handler(void);
void SysTick_Handler(void) { ktos_timer_irq_handler(); }

__attribute__((noreturn)) void ktos_Emergency(const char *msg)
{
    uart_puts("\r\n[KTOS FATAL] "); uart_puts(msg); while (1) {}
}
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

static WORD cmd_task(WORD MsgType, WORD Param1, LONG Param2)
{
    (void)Param1; (void)Param2;
    if (MsgType == KTOS_MSG_TYPE_INIT) {
        uart_puts("=============================\r\n");
        uart_puts("  KTOS UART — Blue Pill\r\n");
        uart_puts("  USART1  115200 8N1\r\n");
        uart_puts("  Type HELP for commands.\r\n");
        uart_puts("=============================\r\n");
        return KTOS_MSG_SLEEP_INDEFINITLY;
    }
    if (MsgType != MSG_LINE_READY) { return KTOS_MSG_SLEEP_INDEFINITLY; }
    if (strcmp(g_line, "PING") == 0)       { uart_puts("PONG\r\n"); }
    else if (strcmp(g_line, "INFO") == 0)  {
        uart_puts("Board : STM32F103C8T6 Blue Pill\r\n");
        uart_puts("Core  : ARM Cortex-M3 @ 72 MHz\r\n");
        uart_puts("RAM   : 20 KB   Flash: 64 KB\r\n");
        uart_puts("OS    : KTOS (cooperative)\r\n");
        uart_puts("Tick  : SysTick 1 ms\r\n");
    }
    else if (strcmp(g_line, "HELP") == 0)  {
        uart_puts("Commands: PING  INFO  HELP\r\n");
    }
    else if (g_line[0] != '\0') {
        uart_puts("Unknown: "); uart_puts(g_line); uart_puts("\r\nType HELP.\r\n");
    }
    return KTOS_MSG_SLEEP_INDEFINITLY;
}

static WORD rx_task(WORD MsgType, WORD Param1, LONG Param2)
{
    (void)Param1; (void)Param2;
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
    usart1_init();
    g_cmd_task = ktos_InitTask(cmd_task, 256, 4, 'C');
    ktos_InitTask(rx_task, 256, 4, 'R');
    ktos_RunOS();
    return 0;
}
