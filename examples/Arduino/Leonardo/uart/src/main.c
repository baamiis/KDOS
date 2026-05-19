/*
 * KTOS — Arduino Leonardo UART example (bare-metal)
 *
 * Two cooperating KTOS tasks demonstrate the canonical "producer /
 * consumer" pattern with the hardware UART.
 *
 *   rx_task  ('R') -- wakes every 10 ms, drains USART1's RX FIFO,
 *                     echoes received bytes, assembles lines, and
 *                     posts MSG_LINE_READY to cmd_task.
 *
 *   cmd_task ('C') -- sleeps with MSG_WAIT.  On MSG_LINE_READY it
 *                     parses PING / INFO / HELP and prints the response.
 *
 * *** Leonardo UART note ***
 * The ATmega32U4 on the Leonardo has native USB hardware.  Without the
 * Arduino USB CDC stack there is no serial-over-USB.  This example uses
 * USART1 — the hardware UART exposed on D0 (RX, PD2) and D1 (TX, PD3).
 *
 * Wiring a USB-to-serial adapter (FTDI / CP2102 / CH340):
 *   adapter TX  ->  Leonardo D0 (RX)
 *   adapter RX  ->  Leonardo D1 (TX)
 *   adapter GND ->  Leonardo GND
 *
 * No Arduino framework, no Serial — direct USART1 register access only.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <string.h>

#include "../../../../../core/ktos.h"

/* =========================================================================
 * USART1 — 115200 8N1 (U2X1=1, UBRR1=16)
 * TX = D1 / PD3,  RX = D0 / PD2
 * ========================================================================= */

static void uart_init(void)
{
    UBRR1H = 0;
    UBRR1L = 16;
    UCSR1A = (1 << U2X1);
    UCSR1B = (1 << TXEN1) | (1 << RXEN1);
    UCSR1C = (1 << UCSZ11) | (1 << UCSZ10);
}

static void uart_putc(char c)
{
    while (!(UCSR1A & (1 << UDRE1))) { }
    UDR1 = (uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s) { uart_putc(*s++); }
}

static int16_t uart_get_byte(void)
{
    if (!(UCSR1A & (1 << RXC1))) { return -1; }
    return (int16_t)UDR1;
}

/* =========================================================================
 * KTOS platform callbacks
 * ========================================================================= */

__attribute__((noreturn))
void ktos_Emergency(const char *msg)
{
    uart_puts("\r\n[KTOS FATAL] ");
    uart_puts(msg);
    uart_puts("\r\n");
    while (1) { }
}

void ktos_DebugPrintf(const char *fmt, ...) { (void)fmt; }
void ktos_InitSys(void) { }

ISR(TIMER1_COMPA_vect) { ktos_timer_irq_handler(); }

/* =========================================================================
 * Application configuration & message types
 * ========================================================================= */

#define MSG_LINE_READY  ((WORD)(KTOS_MSG_TYPE_SYSTEM_START + 1))
#define LINE_BUF_SIZE   64U
#define RX_POLL_MS      10U

static char    g_line_buf[LINE_BUF_SIZE];
static uint8_t g_assembling_len = 0;

static struct ktos_TASK *g_cmd_task = NULL;

/* =========================================================================
 * Command task — consumes MSG_LINE_READY
 * ========================================================================= */

static void print_info(void)
{
    uart_puts("Board   : Arduino Leonardo (ATmega32U4)\r\n");
    uart_puts("Clock   : 16 MHz\r\n");
    uart_puts("SRAM    : 2.5 KB\r\n");
    uart_puts("Flash   : 32 KB\r\n");
    uart_puts("UART    : USART1, D0(RX)/D1(TX)\r\n");
    uart_puts("Baud    : 115200, 8N1\r\n");
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
    (void)sParam;
    (void)lParam;

    if (MsgType == KTOS_MSG_TYPE_INIT) {
        uart_puts("=============================\r\n");
        uart_puts("  KTOS UART Example\r\n");
        uart_puts("  Arduino Leonardo\r\n");
        uart_puts("  USART1: D0(RX), D1(TX)\r\n");
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
        uart_puts("\r\n");
        uart_puts("Type HELP for commands.\r\n");
    }

    return MSG_WAIT;
}

/* =========================================================================
 * RX task — drains USART1, echoes, assembles lines
 * ========================================================================= */

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

/* =========================================================================
 * Entry point
 * ========================================================================= */

int main(void)
{
    uart_init();

    g_cmd_task = ktos_InitTask(cmd_task, 80, 4, 'C');
    ktos_InitTask(rx_task, 64, 2, 'R');

    ktos_RunOS();
    return 0;
}
