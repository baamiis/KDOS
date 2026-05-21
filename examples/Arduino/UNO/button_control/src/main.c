/*
 * KTOS — Arduino UNO button control example (bare-metal)
 *
 * Two cooperating KTOS tasks:
 *
 *   button_task ('B') -- wakes every 5 ms, samples D2 (PD2) with the
 *                        internal pull-up, applies a 30 ms settled-
 *                        state debounce, and on every confirmed edge
 *                        publishes:
 *                            ktos_SendMsg(ui_task, MSG_BUTTON_EVENT,
 *                                         pressed ? 1 : 0, 0);
 *
 *   ui_task     ('I') -- sleeps with KTOS_MSG_SLEEP_INDEFINITLY until a button event
 *                        arrives, then prints "Button pressed" or
 *                        "Button released" over USART0.
 *
 * Connect a button between D2 and GND; the internal pull-up drives D2
 * high when the button is open.
 *
 * D2 on the UNO is PD2 (same as on the Nano).
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <stdbool.h>

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

/* =========================================================================
 * Button on D2 = PD2
 * ========================================================================= */

#define BUTTON_DDR   DDRD
#define BUTTON_PORT  PORTD
#define BUTTON_PIN   PIND
#define BUTTON_BIT   PORTD2

static inline void button_init(void)
{
    BUTTON_DDR  &= (uint8_t)~(1 << BUTTON_BIT);
    BUTTON_PORT |=  (1 << BUTTON_BIT);
}

static inline bool button_raw_high(void)
{
    return (BUTTON_PIN & (1 << BUTTON_BIT)) != 0;
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
 * Message types
 * ========================================================================= */

#define MSG_BUTTON_EVENT ((WORD)(KTOS_MSG_TYPE_SYSTEM_START + 1))
#define POLL_PERIOD_MS   5U
#define DEBOUNCE_MS      30U

static struct ktos_TASK *g_ui_task = NULL;

/* =========================================================================
 * UI task — prints button events
 * ========================================================================= */

static WORD ui_task(WORD MsgType, WORD Param1, LONG Param2)
{
    (void)Param2;

    if (MsgType == KTOS_MSG_TYPE_INIT) {
        uart_puts("=============================\r\n");
        uart_puts("  KTOS Button Control\r\n");
        uart_puts("  Arduino UNO\r\n");
        uart_puts("=============================\r\n");
        uart_puts("Connect button: D2 to GND\r\n");
        return KTOS_MSG_SLEEP_INDEFINITLY;
    }

    if (MsgType == MSG_BUTTON_EVENT) {
        uart_puts(Param1 ? "Button pressed\r\n" : "Button released\r\n");
    }

    return KTOS_MSG_SLEEP_INDEFINITLY;
}

/* =========================================================================
 * Button task — debouncer, publishes confirmed edges
 * ========================================================================= */

static bool     g_stable_pressed = false;
static bool     g_last_raw_high  = true;
static uint16_t g_debounce_count = 0;

static WORD button_task(WORD MsgType, WORD Param1, LONG Param2)
{
    (void)Param1;
    (void)Param2;

    if (MsgType == KTOS_MSG_TYPE_INIT) {
        button_init();
        g_last_raw_high  = button_raw_high();
        g_stable_pressed = !g_last_raw_high;
        g_debounce_count = 0;
        return POLL_PERIOD_MS;
    }

    const bool raw_high = button_raw_high();

    if (raw_high != g_last_raw_high) {
        g_last_raw_high  = raw_high;
        g_debounce_count = 0;
        return POLL_PERIOD_MS;
    }

    const uint16_t needed = DEBOUNCE_MS / POLL_PERIOD_MS;
    if (g_debounce_count < needed) {
        ++g_debounce_count;
        if (g_debounce_count == needed) {
            const bool pressed_now = !raw_high;
            if (pressed_now != g_stable_pressed) {
                g_stable_pressed = pressed_now;
                ktos_SendMsg(g_ui_task, MSG_BUTTON_EVENT,
                             pressed_now ? 1 : 0, 0);
            }
        }
    }

    return POLL_PERIOD_MS;
}

/* =========================================================================
 * Entry point
 * ========================================================================= */

int main(void)
{
    uart_init();

    g_ui_task = ktos_InitTask(ui_task,     80, 4, 'I');
    ktos_InitTask(button_task, 48, 2, 'B');

    ktos_RunOS();
    return 0;
}
