/*
 * KTOS - Arduino Due UART example
 *
 * Two cooperating KTOS tasks form a tiny shell over the Programming
 * Port UART:
 *
 *   rx_task  ('R') -- wakes every 10 ms, drains UART RX, echoes every
 *                     byte (CRLF translated), assembles a line in a
 *                     fixed buffer.  On '\r' or '\n' it posts
 *                     ktos_SendMsg(MSG_LINE_READY) to cmd_task.
 *
 *   cmd_task ('C') -- sleeps with MSG_WAIT.  On MSG_LINE_READY it
 *                     parses one of:
 *                       PING -> PONG
 *                       INFO -> board info
 *                       HELP -> command list
 *
 * No Arduino API - direct register access for UART.
 */

#include "sam.h"
#include <stdint.h>
#include <string.h>

extern "C" {
#include "../../../../../core/ktos.h"
}

/* =========================================================================
 * Programming Port UART
 * ========================================================================= */

static void uart_init(void)
{
    PMC->PMC_PCER0  = (1u << ID_UART);
    PIOA->PIO_ABSR &= ~(PIO_PA8 | PIO_PA9);
    PIOA->PIO_PDR   =  (PIO_PA8 | PIO_PA9);
    UART->UART_CR   = UART_CR_RSTRX | UART_CR_RSTTX | UART_CR_RXDIS | UART_CR_TXDIS;
    UART->UART_MR   = UART_MR_PAR_NO | UART_MR_CHMODE_NORMAL;
    UART->UART_PTCR = UART_PTCR_RXTDIS | UART_PTCR_TXTDIS;
    UART->UART_IDR  = 0xFFFFFFFFu;
    UART->UART_BRGR = 46;
    UART->UART_CR   = UART_CR_RXEN | UART_CR_TXEN;
}

static void uart_putc(char c)
{
    while (!(UART->UART_SR & UART_SR_TXRDY)) { }
    UART->UART_THR = (uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s) { uart_putc(*s++); }
}

static int16_t uart_get_byte(void)
{
    if (!(UART->UART_SR & UART_SR_RXRDY)) { return -1; }
    return (int16_t)(UART->UART_RHR & 0xFFu);
}

/* =========================================================================
 * KTOS callbacks + tick ISR
 * ========================================================================= */

extern "C" __attribute__((noreturn))
void ktos_Emergency(const char *msg)
{
    uart_puts("\r\n[KTOS FATAL] ");
    uart_puts(msg);
    uart_puts("\r\n");
    while (1) { }
}

extern "C" void ktos_DebugPrintf(const char *fmt, ...) { (void)fmt; }
extern "C" void ktos_InitSys(void) { }

extern "C" void ktos_timer_irq_handler(void);

extern "C" void TC0_Handler(void)
{
    (void)TC0->TC_CHANNEL[0].TC_SR;
    ktos_timer_irq_handler();
}

/* =========================================================================
 * Line buffer + cmd_task
 * ========================================================================= */

#define MSG_LINE_READY ((WORD)(KTOS_MSG_TYPE_SYSTEM_START + 1))
#define LINE_BUF_SIZE  64U
#define RX_POLL_MS     10U

static char    g_line_buf[LINE_BUF_SIZE];
static uint8_t g_assembling_len = 0;

static struct ktos_TASK *g_cmd_task = NULL;

static void print_info(void)
{
    uart_puts("Board   : Arduino Due\r\n");
    uart_puts("MCU     : SAM3X8E @ 84 MHz (Cortex-M3)\r\n");
    uart_puts("OS      : KTOS (cooperative)\r\n");
    uart_puts("SRAM    : 96 KB     Flash: 512 KB\r\n");
    uart_puts("Tick    : TC0 channel 0, 1 ms\r\n");
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
    (void)sParam;
    (void)lParam;

    if (MsgType == KTOS_MSG_TYPE_INIT) {
        uart_puts("=============================\r\n");
        uart_puts("  KTOS UART Example (Arduino Due)\r\n");
        uart_puts("=============================\r\n");
        uart_puts("Type HELP for commands.\r\n");
        return MSG_WAIT;
    }
    if (MsgType != MSG_LINE_READY) { return MSG_WAIT; }

    if (strcmp(g_line_buf, "PING") == 0) {
        uart_puts("PONG\r\n");
    } else if (strcmp(g_line_buf, "INFO") == 0) {
        print_info();
    } else if (strcmp(g_line_buf, "HELP") == 0) {
        print_help();
    } else if (g_line_buf[0] != '\0') {
        uart_puts("Unknown: ");
        uart_puts(g_line_buf);
        uart_puts("\r\nType HELP for commands.\r\n");
    }
    return MSG_WAIT;
}

static WORD rx_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam;
    (void)lParam;

    if (MsgType == KTOS_MSG_TYPE_INIT) {
        g_assembling_len = 0;
        return RX_POLL_MS;
    }

    int16_t b;
    while ((b = uart_get_byte()) >= 0) {
        char c = (char)b;

        uart_putc(c);
        if (c == '\r') { uart_putc('\n'); }

        if (c == '\r' || c == '\n') {
            if (g_assembling_len > 0) {
                g_line_buf[g_assembling_len] = '\0';
                ktos_SendMsg(g_cmd_task, MSG_LINE_READY,
                             (WORD)g_assembling_len, 0);
                g_assembling_len = 0;
            }
            continue;
        }

        const char upper = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
        if (g_assembling_len < (LINE_BUF_SIZE - 1)) {
            g_line_buf[g_assembling_len++] = upper;
        }
    }
    return RX_POLL_MS;
}

extern "C" void setup(void)
{
    WDT->WDT_MR = WDT_MR_WDDIS;
    uart_init();

    g_cmd_task = ktos_InitTask(cmd_task, 256, 4, 'C');
    ktos_InitTask(rx_task,                256, 4, 'R');
    ktos_RunOS();
}

extern "C" void loop(void) { }
