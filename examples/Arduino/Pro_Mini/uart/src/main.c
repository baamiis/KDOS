/*
 * KTOS — Arduino Nano UART example (bare-metal)
 *
 * Two cooperating KTOS tasks demonstrate the canonical "producer /
 * consumer" pattern with a single hardware UART.  No Arduino runtime
 * is involved — USART0 is driven directly through its registers.
 *
 *   rx_task  ('R') -- wakes every 10 ms, drains USART0's RX FIFO into
 *                     a fixed 64-byte buffer.  Echoes every received
 *                     byte (CRLF translated for terminal hygiene).
 *                     When a newline is seen, the assembled line is
 *                     copied into a shared buffer and:
 *                         ktos_SendMsg(cmd_task, MSG_LINE_READY, len, 0);
 *
 *   cmd_task ('C') -- sleeps with KTOS_MSG_SLEEP_INDEFINITLY.  On MSG_LINE_READY it
 *                     parses the line (PING / INFO / HELP) and writes
 *                     the response.  Uses strcmp on a fixed char
 *                     array — no String, no heap.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <string.h>

#include "../../../../../core/ktos.h"

/* =========================================================================
 * USART0 — 115200 8N1 (U2X0=1, UBRR0=16)
 * ========================================================================= */

static void uart_init(void)
{
    UBRR0H = 0;
    UBRR0L = 16;
    UCSR0A = (1 << U2X0);
    UCSR0B = (1 << TXEN0) | (1 << RXEN0);
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

static void uart_putc(char c)
{
    while (!(UCSR0A & (1 << UDRE0))) { }
    UDR0 = (uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s) { uart_putc(*s++); }
}

static int16_t uart_get_byte(void)
{
    if (!(UCSR0A & (1 << RXC0))) { return -1; }
    return (int16_t)UDR0;
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

/* =========================================================================
 * KTOS 1 ms tick — Timer1 CTC
 * ========================================================================= */

extern void ktos_timer_irq_handler(void);

ISR(TIMER1_COMPA_vect)
{
    ktos_timer_irq_handler();
}

/* =========================================================================
 * Application configuration & message types
 * ========================================================================= */

#define MSG_LINE_READY  ((WORD)(KTOS_MSG_TYPE_SYSTEM_START + 1))

#define LINE_BUF_SIZE   64U
#define RX_POLL_MS      10U

/* The completed line lives in a single shared buffer.  Because KTOS is
 * cooperative, rx_task cannot overwrite it while cmd_task is running
 * — by the time cmd_task is dispatched, rx_task has already yielded
 * and will not be redispatched until cmd_task returns. */
static char    g_line_buf[LINE_BUF_SIZE];
static uint8_t g_assembling_len = 0;

static struct ktos_TASK *g_cmd_task = NULL;

/* =========================================================================
 * Command task — consumes MSG_LINE_READY
 * ========================================================================= */

static void print_info(void)
{
    uart_puts("Board   : Arduino Nano (ATmega328P)\r\n");
    uart_puts("Clock   : 16 MHz\r\n");
    uart_puts("SRAM    : 2 KB\r\n");
    uart_puts("Flash   : 32 KB\r\n");
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
        uart_puts("=============================\r\n");
        uart_puts("Type HELP for commands.\r\n");
        return KTOS_MSG_SLEEP_INDEFINITLY;
    }

    if (MsgType != MSG_LINE_READY) {
        return KTOS_MSG_SLEEP_INDEFINITLY;
    }

    /* g_line_buf is guaranteed null-terminated by rx_task. */
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

    return KTOS_MSG_SLEEP_INDEFINITLY;
}

/* =========================================================================
 * RX task — drains USART0, echoes, assembles a line, posts MSG_LINE_READY
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

        /* Echo immediately for a responsive feel. */
        uart_putc(c);
        if (c == '\r') { uart_putc('\n'); }   /* normalise echo to CRLF */

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
        /* Overflow: silently drop the rest of the line. */
    }

    return RX_POLL_MS;
}

/* =========================================================================
 * Entry point
 * ========================================================================= */

int main(void)
{
    uart_init();

    /* cmd_task first so rx_task has a valid handle to send to. */
    g_cmd_task = ktos_InitTask(cmd_task,
                               /* StackSize = */ 80,
                               /* QueueSize = */ 4,
                               /* TaskID    = */ 'C');

    ktos_InitTask(rx_task,
                  /* StackSize = */ 64,
                  /* QueueSize = */ 2,
                  /* TaskID    = */ 'R');

    ktos_RunOS();    /* never returns */
    return 0;        /* unreachable */
}
