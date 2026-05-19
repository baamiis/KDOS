/*
 * KTOS — Arduino Zero UART example (bare-metal)
 *
 * Two cooperating KTOS tasks:
 *   rx_task  ('R') -- wakes every 10 ms, drains SERCOM5 RX, assembles
 *                     lines, posts MSG_LINE_READY to cmd_task.
 *   cmd_task ('C') -- sleeps with MSG_WAIT; on MSG_LINE_READY parses
 *                     PING / INFO / HELP and prints the response.
 *
 * Serial port: PROGRAMMING port (near RESET button) via on-board EDBG.
 * EDBG bridges that USB-CDC to SERCOM5 USART:
 *   TX = PA22  (SERCOM5 PAD[0], peripheral D)
 *   RX = PA23  (SERCOM5 PAD[1], peripheral D)
 *
 * Clock: 48 MHz (DFLL48M, configured in bsp/samd21g18/startup.c).
 * KTOS tick: TC3 (IRQ 18), 1 ms.
 * Baud: 115200 — BAUD register = 63019.
 */

#include <stdint.h>
#include <string.h>
#include "../../../../../core/ktos.h"

/* =========================================================================
 * PM / GCLK — peripheral clock enables
 * ========================================================================= */
#define PM_APBCMASK  (*(volatile uint32_t *)0x40000420UL)
#define GCLK_CLKCTRL (*(volatile uint16_t *)0x40000C02UL)
#define GCLK_STATUS  (*(volatile uint8_t  *)0x40000C01UL)

/* =========================================================================
 * PORT A
 * ========================================================================= */
#define PORTA_DIRSET  (*(volatile uint32_t *)0x41004408UL)
#define PORTA_PMUX11  (*(volatile uint8_t  *)0x4100443BUL) /* PA22/PA23 mux */
#define PORTA_PINCFG22 (*(volatile uint8_t *)0x41004456UL)
#define PORTA_PINCFG23 (*(volatile uint8_t *)0x41004457UL)

/* =========================================================================
 * SERCOM5 USART — 115200 8N1 at 48 MHz
 * Base: 0x42001C00.  TX=PA22/PAD[0], RX=PA23/PAD[1], both peripheral D.
 * BAUD = 65536*(1 - 16*115200/48000000) = 63019
 * ========================================================================= */
#define SERCOM5_CTRLA   (*(volatile uint32_t *)0x42001C00UL)
#define SERCOM5_CTRLB   (*(volatile uint32_t *)0x42001C04UL)
#define SERCOM5_BAUD    (*(volatile uint16_t *)0x42001C0CUL)
#define SERCOM5_INTFLAG (*(volatile uint8_t  *)0x42001C18UL)
#define SERCOM5_STATUS  (*(volatile uint16_t *)0x42001C1AUL)
#define SERCOM5_SYNCBUSY (*(volatile uint32_t*)0x42001C1CUL)
#define SERCOM5_DATA    (*(volatile uint16_t *)0x42001C28UL)

#define USART_INTFLAG_RXC  (1u << 2)
#define USART_INTFLAG_DRE  (1u << 0)

static void sercom5_init(void)
{
    /* Enable SERCOM5 APB clock (APBCMASK bit 7) */
    PM_APBCMASK |= (1u << 7);

    /* Connect GCLK0 (48 MHz) to SERCOM5 (GCLK peripheral ID 25 = SERCOM5_CORE) */
    GCLK_CLKCTRL = (uint16_t)((25u) | (0u << 8) | (1u << 14));
    while (GCLK_STATUS & (1u << 7)) {}

    /* PA22 → SERCOM5 PAD[0] TX (peripheral D = mux value 3) */
    PORTA_PMUX11   = (3u << 4) | 3u;    /* both PA22 (even) and PA23 (odd) → D */
    PORTA_PINCFG22 = (1u << 0);          /* PMUXEN, no input */
    PORTA_PINCFG23 = (1u << 0) | (1u << 1); /* PMUXEN | INEN for RX */

    /* Reset SERCOM5 */
    SERCOM5_CTRLA = (1u << 0);  /* SWRST */
    while (SERCOM5_SYNCBUSY & (1u << 0)) {}

    /* CTRLA: MODE=USART_INT_CLK(1<<2=0x4 in bits[4:2]), DORD=LSB(1<<28), RXPO=PAD1(1<<20) */
    SERCOM5_CTRLA = (0x4u << 0) | (1u << 20) | (1u << 28);
    /* CTRLB: TXEN | RXEN */
    SERCOM5_CTRLB = (1u << 16) | (1u << 17);
    while (SERCOM5_SYNCBUSY & (1u << 2)) {}  /* wait CTRLB sync */
    /* BAUD for 115200 at 48 MHz */
    SERCOM5_BAUD  = 63019u;
    /* Enable */
    SERCOM5_CTRLA |= (1u << 1);
    while (SERCOM5_SYNCBUSY & (1u << 1)) {}
}

static void uart_putc(char c)
{
    while (!(SERCOM5_INTFLAG & USART_INTFLAG_DRE)) {}
    SERCOM5_DATA = (uint8_t)c;
}
static void uart_puts(const char *s) { while (*s) { uart_putc(*s++); } }
static int  uart_getc(void)
{
    if (!(SERCOM5_INTFLAG & USART_INTFLAG_RXC)) { return -1; }
    return (int)(SERCOM5_DATA & 0xFFu);
}

/* TC3 — KTOS tick */
void TC3_Handler(void)
{
    *(volatile uint8_t *)0x42002C0EUL = 1u; /* clear INTFLAG.OVF */
    ktos_timer_irq_handler();
}

__attribute__((noreturn))
void ktos_Emergency(const char *msg) { uart_puts("\r\n[KTOS FATAL] "); uart_puts(msg); while (1) {} }
void ktos_DebugPrintf(const char *f, ...) { (void)f; }
void ktos_InitSys(void) {}

/* =========================================================================
 * Application
 * ========================================================================= */
#define MSG_LINE_READY ((WORD)(KTOS_MSG_TYPE_SYSTEM_START + 1))
#define LINE_BUF_SIZE  64U
#define RX_POLL_MS     10U

static char    g_line_buf[LINE_BUF_SIZE];
static uint8_t g_asm_len = 0;
static struct ktos_TASK *g_cmd_task = NULL;

static void print_info(void)
{
    uart_puts("Board   : Arduino Zero (ATSAMD21G18 / Cortex-M0+)\r\n");
    uart_puts("Clock   : 48 MHz (DFLL48M)\r\n");
    uart_puts("SRAM    : 32 KB\r\n");
    uart_puts("Flash   : 256 KB\r\n");
    uart_puts("Baud    : 115200 via EDBG programming port\r\n");
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
        uart_puts("  Arduino Zero (SAMD21G18)\r\n");
        uart_puts("=============================\r\n");
        uart_puts("Type HELP for commands.\r\n");
        return MSG_WAIT;
    }
    if (MsgType != MSG_LINE_READY) { return MSG_WAIT; }

    if      (strcmp(g_line_buf, "PING") == 0) { uart_puts("PONG\r\n"); }
    else if (strcmp(g_line_buf, "INFO") == 0) { print_info(); }
    else if (strcmp(g_line_buf, "HELP") == 0) { print_help(); }
    else if (g_line_buf[0]) {
        uart_puts("Unknown: "); uart_puts(g_line_buf); uart_puts("\r\n");
    }
    return MSG_WAIT;
}

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
        if (g_asm_len < LINE_BUF_SIZE - 1u) { g_line_buf[g_asm_len++] = up; }
    }
    return RX_POLL_MS;
}

int main(void)
{
    sercom5_init();
    g_cmd_task = ktos_InitTask(cmd_task, 256, 4, 'C');
    ktos_InitTask(rx_task, 128, 2, 'R');
    ktos_RunOS();
    return 0;
}
