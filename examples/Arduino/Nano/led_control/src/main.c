/*
 * KTOS — Arduino Nano LED control example (bare-metal)
 *
 * Two cooperating KTOS tasks:
 *
 *   uart_task  ('U')  -- polls USART0 RX every 20 ms, builds a line in
 *                        a fixed 16-byte buffer, parses ON/OFF/BLINK,
 *                        and posts MSG_SET_MODE to led_task.
 *
 *   led_task   ('L')  -- owns D13 (PB5).  On MSG_SET_MODE it switches
 *                        between OFF / ON / BLINK.  In BLINK mode it
 *                        returns 500 ms to toggle the LED on every
 *                        KTOS_MSG_TYPE_TIMER; in ON / OFF it returns
 *                        MSG_WAIT and stays idle until the next msg.
 *
 * No Arduino framework — direct register access to USART0 and PORTB.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <stdbool.h>
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

/* Non-blocking RX: returns -1 if no byte is ready. */
static int16_t uart_get_byte(void)
{
    if (!(UCSR0A & (1 << RXC0))) { return -1; }
    return (int16_t)UDR0;
}

/* =========================================================================
 * LED on D13 = PB5
 * ========================================================================= */

#define LED_DDR   DDRB
#define LED_PORT  PORTB
#define LED_BIT   PORTB5

static inline void led_init(void)  { LED_DDR |= (1 << LED_BIT); }
static inline void led_on(void)    { LED_PORT |= (1 << LED_BIT); }
static inline void led_off(void)   { LED_PORT &= (uint8_t)~(1 << LED_BIT); }
static inline void led_toggle(void){ LED_PORT ^= (1 << LED_BIT); }

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
 * Application message types — must be > KTOS_MSG_TYPE_SYSTEM_START
 * ========================================================================= */

#define MSG_SET_MODE ((WORD)(KTOS_MSG_TYPE_SYSTEM_START + 1))

#define MODE_OFF    0
#define MODE_ON     1
#define MODE_BLINK  2

#define BLINK_PERIOD_MS  500U
#define UART_POLL_MS     20U
#define CMD_BUF_SIZE     16U

/* Forward handle so uart_task can post messages to led_task. */
static struct ktos_TASK *g_led_task = NULL;

/* =========================================================================
 * LED task — owns the LED, switches modes on MSG_SET_MODE
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
            return MSG_WAIT;        /* solid ON/OFF — nothing to do */

        case MSG_SET_MODE:
            g_led_mode = (uint8_t)sParam;
            switch (g_led_mode) {
                case MODE_ON:
                    g_led_level = true;
                    led_on();
                    uart_puts("LED ON\r\n");
                    return MSG_WAIT;
                case MODE_OFF:
                    g_led_level = false;
                    led_off();
                    uart_puts("LED OFF\r\n");
                    return MSG_WAIT;
                case MODE_BLINK:
                default:
                    g_led_mode = MODE_BLINK;
                    uart_puts("LED BLINK\r\n");
                    return BLINK_PERIOD_MS;
            }
    }

    return MSG_WAIT;
}

/* =========================================================================
 * UART task — drains USART0, parses lines, posts MSG_SET_MODE
 * ========================================================================= */

static char    g_cmd_buf[CMD_BUF_SIZE];
static uint8_t g_cmd_len = 0;

static void dispatch_line(const char *line)
{
    uart_puts("Command: ");
    uart_puts(line);
    uart_puts("\r\n");

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
        uart_puts("=============================\r\n");
        uart_puts("Commands: ON, OFF, BLINK\r\n");
        return UART_POLL_MS;
    }

    /* KTOS_MSG_TYPE_TIMER — drain whatever USART0 has buffered. */
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
        /* Overflow: silently drop until newline. */
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

    /* led_task first so uart_task has a valid handle to send to. */
    g_led_task = ktos_InitTask(led_task,
                               /* StackSize  = */ 64,
                               /* QueueSize  = */ 4,
                               /* TaskID     = */ 'L');

    ktos_InitTask(uart_task,
                  /* StackSize  = */ 80,
                  /* QueueSize  = */ 4,
                  /* TaskID     = */ 'U');

    ktos_RunOS();    /* never returns */
    return 0;        /* unreachable */
}
