/*
 * KTOS — Arduino Mega 2560 button control example (bare-metal)
 *
 * Two cooperating KTOS tasks:
 *
 *   button_task ('B') -- wakes every 5 ms, samples D2 (PE4) with the
 *                        internal pull-up, applies a 30 ms debounce,
 *                        and on every confirmed edge publishes:
 *                            ktos_SendMsg(ui_task, MSG_BUTTON_EVENT,
 *                                         pressed ? 1 : 0, 0);
 *
 *   ui_task     ('I') -- sleeps with KTOS_MSG_SLEEP_INDEFINITLY until a button event
 *                        arrives, then prints "Button pressed" or
 *                        "Button released" over USART0.
 *
 * Note: D2 on the Mega 2560 is PE4, not PD2 as on the Uno/Nano.
 * Connect a button between D2 and GND; internal pull-up drives D2 high
 * when the button is open.
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
 * Button on D2 = PE4 (Mega 2560)
 * ========================================================================= */

#define BTN_DDR   DDRE
#define BTN_PORT  PORTE
#define BTN_PIN   PINE
#define BTN_BIT   PINE4

static inline void btn_init(void)
{
    BTN_DDR  &= (uint8_t)~(1 << BTN_BIT);   /* input */
    BTN_PORT |=  (1 << BTN_BIT);             /* pull-up enabled */
}

static inline bool btn_pressed(void)
{
    return !(BTN_PIN & (1 << BTN_BIT));      /* active-low */
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
#define BTN_POLL_MS      5U
#define DEBOUNCE_MS      30U
#define DEBOUNCE_TICKS   (DEBOUNCE_MS / BTN_POLL_MS)

static struct ktos_TASK *g_ui_task = NULL;

/* =========================================================================
 * Button task — polls and debounces
 * ========================================================================= */

static WORD button_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam;
    (void)lParam;

    static bool    last_stable  = false;
    static bool    candidate    = false;
    static uint8_t stable_ticks = 0;

    if (MsgType == KTOS_MSG_TYPE_INIT) {
        btn_init();
        last_stable = btn_pressed();
        return BTN_POLL_MS;
    }

    bool raw = btn_pressed();

    if (raw == candidate) {
        if (stable_ticks < DEBOUNCE_TICKS) {
            stable_ticks++;
        }
    } else {
        candidate    = raw;
        stable_ticks = 0;
    }

    if (stable_ticks >= DEBOUNCE_TICKS && candidate != last_stable) {
        last_stable = candidate;
        ktos_SendMsg(g_ui_task, MSG_BUTTON_EVENT, last_stable ? 1u : 0u, 0);
    }

    return BTN_POLL_MS;
}

/* =========================================================================
 * UI task — prints events
 * ========================================================================= */

static WORD ui_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)lParam;

    if (MsgType == KTOS_MSG_TYPE_INIT) {
        uart_puts("=============================\r\n");
        uart_puts("  KTOS Button Control\r\n");
        uart_puts("  Arduino Mega 2560\r\n");
        uart_puts("=============================\r\n");
        uart_puts("Connect button: D2 (PE4) to GND\r\n");
        return KTOS_MSG_SLEEP_INDEFINITLY;
    }

    if (MsgType == MSG_BUTTON_EVENT) {
        uart_puts(sParam ? "Button pressed\r\n" : "Button released\r\n");
    }

    return KTOS_MSG_SLEEP_INDEFINITLY;
}

/* =========================================================================
 * Entry point
 * ========================================================================= */

int main(void)
{
    uart_init();

    g_ui_task = ktos_InitTask(ui_task,     64, 4, 'I');
    ktos_InitTask(button_task, 64, 4, 'B');

    ktos_RunOS();
    return 0;
}
