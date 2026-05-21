/*
 * KTOS — Arduino Leonardo LED control example (bare-metal)
 *
 * Two cooperating KTOS tasks:
 *
 *   uart_task  ('U')  -- polls USART1 RX every 20 ms, builds a line,
 *                        parses ON/OFF/BLINK, posts MSG_SET_MODE to led_task.
 *
 *   led_task   ('L')  -- owns D13 (PC7).  Switches between OFF / ON / BLINK.
 *
 * D13 on the Leonardo is PC7 (differs from UNO/Nano where it is PB5).
 *
 * Serial output uses USART1 on D0(RX)/D1(TX).  Connect a USB-to-serial
 * adapter — adapter-RX → D1, adapter-TX → D0.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "../../../../../core/ktos.h"

/* =========================================================================
 * USART1 — 115200 8N1 (U2X1=1, UBRR1=16)
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
 * LED on D13 = PC7 (Leonardo — differs from UNO/Nano PB5)
 * ========================================================================= */

#define LED_DDR   DDRC
#define LED_PORT  PORTC
#define LED_BIT   PC7

static inline void led_init(void)   { LED_DDR  |=  (1 << LED_BIT); }
static inline void led_on(void)     { LED_PORT |=  (1 << LED_BIT); }
static inline void led_off(void)    { LED_PORT &= (uint8_t)~(1 << LED_BIT); }

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
 * Message types and mode constants
 * ========================================================================= */

#define MSG_SET_MODE    ((WORD)(KTOS_MSG_TYPE_SYSTEM_START + 1))
#define MODE_OFF        0
#define MODE_ON         1
#define MODE_BLINK      2
#define BLINK_PERIOD_MS 500U
#define UART_POLL_MS    20U
#define CMD_BUF_SIZE    16U

static struct ktos_TASK *g_led_task = NULL;

/* =========================================================================
 * LED task
 * ========================================================================= */

static uint8_t g_led_mode  = MODE_BLINK;
static bool    g_led_level = false;

static WORD led_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)lParam;

    switch (MsgType) {
        case KTOS_MSG_TYPE_INIT:
            led_init();
            led_off();
            uart_puts("LED BLINK\r\n");
            g_led_mode = MODE_BLINK;
            return BLINK_PERIOD_MS;

        case KTOS_MSG_TYPE_TIMER:
            if (g_led_mode == MODE_BLINK) {
                g_led_level = !g_led_level;
                if (g_led_level) { led_on(); } else { led_off(); }
                return BLINK_PERIOD_MS;
            }
            return KTOS_MSG_SLEEP_INDEFINITLY;

        case MSG_SET_MODE:
            g_led_mode = (uint8_t)sParam;
            switch (g_led_mode) {
                case MODE_ON:
                    led_on();
                    uart_puts("LED ON\r\n");
                    return KTOS_MSG_SLEEP_INDEFINITLY;
                case MODE_OFF:
                    led_off();
                    uart_puts("LED OFF\r\n");
                    return KTOS_MSG_SLEEP_INDEFINITLY;
                default:
                    g_led_mode = MODE_BLINK;
                    uart_puts("LED BLINK\r\n");
                    return BLINK_PERIOD_MS;
            }
    }

    return KTOS_MSG_SLEEP_INDEFINITLY;
}

/* =========================================================================
 * UART task
 * ========================================================================= */

static char    g_cmd_buf[CMD_BUF_SIZE];
static uint8_t g_cmd_len = 0;

static void dispatch_line(const char *line)
{
    if (strcmp(line, "ON") == 0) {
        ktos_SendMsg(g_led_task, MSG_SET_MODE, MODE_ON, 0);
    } else if (strcmp(line, "OFF") == 0) {
        ktos_SendMsg(g_led_task, MSG_SET_MODE, MODE_OFF, 0);
    } else if (strcmp(line, "BLINK") == 0) {
        ktos_SendMsg(g_led_task, MSG_SET_MODE, MODE_BLINK, 0);
    } else if (line[0] != '\0') {
        uart_puts("Unknown command. Try ON, OFF, BLINK.\r\n");
    }
}

static WORD uart_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam;
    (void)lParam;

    if (MsgType == KTOS_MSG_TYPE_INIT) {
        uart_puts("=============================\r\n");
        uart_puts("  KTOS LED Control Example\r\n");
        uart_puts("  Arduino Leonardo\r\n");
        uart_puts("=============================\r\n");
        uart_puts("Commands: ON, OFF, BLINK\r\n");
        return UART_POLL_MS;
    }

    int16_t b;
    while ((b = uart_get_byte()) >= 0) {
        char c = (char)b;
        if (c == '\r' || c == '\n') {
            if (g_cmd_len > 0) {
                g_cmd_buf[g_cmd_len] = '\0';
                dispatch_line(g_cmd_buf);
                g_cmd_len = 0;
            }
            continue;
        }
        const char upper = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
        if (g_cmd_len < (CMD_BUF_SIZE - 1)) {
            g_cmd_buf[g_cmd_len++] = upper;
        }
    }

    return UART_POLL_MS;
}

/* =========================================================================
 * Entry point
 * ========================================================================= */

int main(void)
{
    uart_init();
    led_init();

    g_led_task = ktos_InitTask(led_task,  64, 4, 'L');
    ktos_InitTask(uart_task, 80, 4, 'U');

    ktos_RunOS();
    return 0;
}
