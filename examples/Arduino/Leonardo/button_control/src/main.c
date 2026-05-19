/*
 * KTOS — Arduino Leonardo button control example (bare-metal)
 *
 * Two cooperating KTOS tasks:
 *
 *   button_task ('B') -- wakes every 5 ms, samples D4 (PD4) with the
 *                        internal pull-up, applies a 30 ms settled-state
 *                        debounce, and on every confirmed edge publishes:
 *                            ktos_SendMsg(ui_task, MSG_BUTTON_EVENT,
 *                                         pressed ? 1 : 0, 0);
 *
 *   ui_task     ('I') -- sleeps with MSG_WAIT until a button event
 *                        arrives, then prints "Button pressed" or
 *                        "Button released" over USART1.
 *
 * Pin choice: D4 = PD4.  D2 is avoided because on the ATmega32U4 D2 maps
 * to PD1, which is the TWI SDA line — using it as GPIO would break I2C.
 *
 * Connect a button between D4 and GND; the internal pull-up drives D4
 * high when the button is open.
 *
 * Serial output uses USART1 on D0(RX)/D1(TX).  Connect a USB-to-serial
 * adapter — adapter-RX → D1, adapter-TX → D0.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <stdbool.h>

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

/* =========================================================================
 * Button on D4 = PD4
 * ========================================================================= */

#define BTN_DDR   DDRD
#define BTN_PORT  PORTD
#define BTN_PIN   PIND
#define BTN_BIT   PD4

static inline void btn_init(void)
{
    BTN_DDR  &= (uint8_t)~(1 << BTN_BIT);
    BTN_PORT |=  (1 << BTN_BIT);
}

static inline bool btn_raw_high(void)
{
    return (BTN_PIN & (1 << BTN_BIT)) != 0;
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

static WORD ui_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)lParam;

    if (MsgType == KTOS_MSG_TYPE_INIT) {
        uart_puts("=============================\r\n");
        uart_puts("  KTOS Button Control\r\n");
        uart_puts("  Arduino Leonardo\r\n");
        uart_puts("=============================\r\n");
        uart_puts("Connect button: D4 (PD4) to GND\r\n");
        return MSG_WAIT;
    }

    if (MsgType == MSG_BUTTON_EVENT) {
        uart_puts(sParam ? "Button pressed\r\n" : "Button released\r\n");
    }

    return MSG_WAIT;
}

/* =========================================================================
 * Button task — debouncer
 * ========================================================================= */

static bool     g_stable_pressed = false;
static bool     g_last_raw_high  = true;
static uint16_t g_debounce_count = 0;

static WORD button_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam;
    (void)lParam;

    if (MsgType == KTOS_MSG_TYPE_INIT) {
        btn_init();
        g_last_raw_high  = btn_raw_high();
        g_stable_pressed = !g_last_raw_high;
        g_debounce_count = 0;
        return POLL_PERIOD_MS;
    }

    const bool raw_high = btn_raw_high();

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
