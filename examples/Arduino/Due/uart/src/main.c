/*
 * KTOS — Arduino Due UART example (bare-metal)
 *
 * Two cooperating KTOS tasks:
 *
 *   rx_task  ('R') -- wakes every 10 ms, drains UART0 RX, assembles
 *                     lines, posts MSG_LINE_READY to cmd_task.
 *
 *   cmd_task ('C') -- sleeps with MSG_WAIT; on MSG_LINE_READY parses
 *                     PING / INFO / HELP and prints the response.
 *
 * Serial port: connect via the PROGRAMMING port (small USB near reset).
 * The onboard ATmega16U2 bridges that port to SAM3X8E UART0:
 *   RX = D0 / PA8   TX = D1 / PA9
 *
 * No Arduino framework — direct SAM3X8E register access only.
 * KTOS tick: TC0 channel 0 (IRQ 27), 1 ms at 84 MHz.
 */

#include <stdint.h>
#include <string.h>
#include "../../../../../core/ktos.h"

/* =========================================================================
 * PMC — enable peripheral clocks
 * ========================================================================= */
#define PMC_PCER0  (*(volatile uint32_t *)0x400E0610UL)

/* =========================================================================
 * PIOA — configure PA8/PA9 as UART0 peripheral A
 * ========================================================================= */
#define PIOA_PDR   (*(volatile uint32_t *)0x400E0E04UL)
#define PIOA_ABSR  (*(volatile uint32_t *)0x400E0E70UL)

/* =========================================================================
 * UART0 — 115200 8N1 at 84 MHz
 *   BRGR = 84000000 / (16 * 115200) ≈ 46  (actual 114130 baud, <1% error)
 * ========================================================================= */
#define UART0_CR   (*(volatile uint32_t *)0x400E0800UL)
#define UART0_MR   (*(volatile uint32_t *)0x400E0804UL)
#define UART0_SR   (*(volatile uint32_t *)0x400E0814UL)
#define UART0_RHR  (*(volatile uint32_t *)0x400E0818UL)
#define UART0_THR  (*(volatile uint32_t *)0x400E081CUL)
#define UART0_BRGR (*(volatile uint32_t *)0x400E0820UL)

#define UART_SR_RXRDY   (1u << 0)
#define UART_SR_TXRDY   (1u << 1)

static void uart_init(void)
{
    PMC_PCER0 = (1u << 11) | (1u << 8);       /* PIOA (11), UART0 (8) */
    PIOA_PDR  = (1u << 8) | (1u << 9);        /* PA8/PA9 → peripheral */
    PIOA_ABSR &= ~((1u << 8) | (1u << 9));    /* select peripheral A */
    UART0_CR   = (1u << 2) | (1u << 3);       /* RSTRX | RSTTX */
    UART0_MR   = (4u << 9);                   /* no parity */
    UART0_BRGR = 46u;
    UART0_CR   = (1u << 4) | (1u << 6);       /* RXEN | TXEN */
}

static void uart_putc(char c)
{
    while (!(UART0_SR & UART_SR_TXRDY)) { }
    UART0_THR = (uint32_t)(uint8_t)c;
}

static void uart_puts(const char *s) { while (*s) { uart_putc(*s++); } }

static int uart_getc(void)
{
    if (!(UART0_SR & UART_SR_RXRDY)) { return -1; }
    return (int)(UART0_RHR & 0xFFu);
}

/* =========================================================================
 * TC0 channel 0 — KTOS 1 ms tick (read TC_SR to clear interrupt flag)
 * ========================================================================= */
#define TC0_CH0_SR (*(volatile uint32_t *)0x40080020UL)

void TC0_Handler(void)
{
    (void)TC0_CH0_SR;
    ktos_timer_irq_handler();
}

/* =========================================================================
 * KTOS platform callbacks
 * ========================================================================= */
__attribute__((noreturn))
void ktos_Emergency(const char *msg)
{
    uart_puts("\r\n[KTOS FATAL] "); uart_puts(msg); uart_puts("\r\n");
    while (1) {}
}
void ktos_DebugPrintf(const char *fmt, ...) { (void)fmt; }
void ktos_InitSys(void) {}

/* =========================================================================
 * Message types
 * ========================================================================= */
#define MSG_LINE_READY ((WORD)(KTOS_MSG_TYPE_SYSTEM_START + 1))
#define LINE_BUF_SIZE  64U
#define RX_POLL_MS     10U

static char    g_line_buf[LINE_BUF_SIZE];
static uint8_t g_asm_len = 0;
static struct ktos_TASK *g_cmd_task = NULL;

/* =========================================================================
 * Command task
 * ========================================================================= */
static void print_info(void)
{
    uart_puts("Board   : Arduino Due (SAM3X8E / Cortex-M3)\r\n");
    uart_puts("Clock   : 84 MHz\r\n");
    uart_puts("SRAM    : 96 KB\r\n");
    uart_puts("Flash   : 512 KB\r\n");
    uart_puts("Baud    : 115200 via programming port\r\n");
    uart_puts("OS      : KTOS\r\n");
}

static void print_help(void)
{
    uart_puts("Commands:\r\n");
    uart_puts("  PING  - reply with PONG\r\n");
    uart_puts("  INFO  - show board info\r\n");
    uart_puts("  HELP  - this list\r\n");
}

static WORD cmd_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam; (void)lParam;
    if (MsgType == KTOS_MSG_TYPE_INIT) {
        uart_puts("=============================\r\n");
        uart_puts("  KTOS UART Example\r\n");
        uart_puts("  Arduino Due (SAM3X8E)\r\n");
        uart_puts("=============================\r\n");
        uart_puts("Type HELP for commands.\r\n");
        return MSG_WAIT;
    }
    if (MsgType != MSG_LINE_READY) { return MSG_WAIT; }

    if      (strcmp(g_line_buf, "PING") == 0) { uart_puts("PONG\r\n"); }
    else if (strcmp(g_line_buf, "INFO") == 0) { print_info(); }
    else if (strcmp(g_line_buf, "HELP") == 0) { print_help(); }
    else if (g_line_buf[0] != '\0') {
        uart_puts("Unknown: "); uart_puts(g_line_buf); uart_puts("\r\n");
    }
    return MSG_WAIT;
}

/* =========================================================================
 * RX task
 * ========================================================================= */
static WORD rx_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam; (void)lParam;
    if (MsgType == KTOS_MSG_TYPE_INIT) { g_asm_len = 0; return RX_POLL_MS; }

    int b;
    while ((b = uart_getc()) >= 0) {
        char c = (char)b;
        uart_putc(c);
        if (c == '\r') { uart_putc('\n'); }
        if (c == '\r' || c == '\n') {
            if (g_asm_len > 0) {
                g_line_buf[g_asm_len] = '\0';
                ktos_SendMsg(g_cmd_task, MSG_LINE_READY, (WORD)g_asm_len, 0);
                g_asm_len = 0;
            }
            continue;
        }
        char up = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
        if (g_asm_len < (LINE_BUF_SIZE - 1)) { g_line_buf[g_asm_len++] = up; }
    }
    return RX_POLL_MS;
}

/* =========================================================================
 * Entry point
 * ========================================================================= */
int main(void)
{
    uart_init();
    g_cmd_task = ktos_InitTask(cmd_task, 128, 4, 'C');
    ktos_InitTask(rx_task, 96, 2, 'R');
    ktos_RunOS();
    return 0;
}
