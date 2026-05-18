/*
 * KTOS - Arduino Due button control example
 *
 * Two cooperating KTOS tasks:
 *
 *   button_task ('B') -- wakes every 5 ms, samples D2 (PB25) with the
 *                        internal pull-up, applies a 30 ms settled-
 *                        state debounce, and on every confirmed edge
 *                        publishes:
 *                            ktos_SendMsg(ui_task, MSG_BUTTON_EVENT,
 *                                         pressed ? 1 : 0, 0);
 *
 *   ui_task     ('I') -- sleeps with MSG_WAIT until a button event
 *                        arrives, then prints "Button pressed" or
 *                        "Button released" via the UART.
 *
 * No Arduino API - direct register access to UART, PIOB.
 */

#include "sam.h"
#include <stdint.h>
#include <stdbool.h>

extern "C" {
#include "../../../../../core/ktos.h"
}

/* =========================================================================
 * Programming Port UART - 115200 8N1
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

/* =========================================================================
 * Button on D2 = PB25 (Arduino Due)
 *
 * Internal pull-up enabled — button wires PB25 to GND.  Active-low.
 * ========================================================================= */

#define BUTTON_BIT_MASK   (1u << 25)

static inline void button_init(void)
{
    PMC->PMC_PCER0 = (1u << ID_PIOB);
    PIOB->PIO_PER  = BUTTON_BIT_MASK;   /* PIO controls the pin */
    PIOB->PIO_ODR  = BUTTON_BIT_MASK;   /* output disable -> input */
    PIOB->PIO_PUER = BUTTON_BIT_MASK;   /* pull-up enable */
}

static inline bool button_raw_high(void)
{
    return (PIOB->PIO_PDSR & BUTTON_BIT_MASK) != 0;
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
 * Application
 * ========================================================================= */

#define MSG_BUTTON_EVENT  ((WORD)(KTOS_MSG_TYPE_SYSTEM_START + 1))
#define POLL_PERIOD_MS    5U
#define DEBOUNCE_MS       30U

static struct ktos_TASK *g_ui_task = NULL;

static WORD ui_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)lParam;

    if (MsgType == KTOS_MSG_TYPE_INIT) {
        uart_puts("=============================\r\n");
        uart_puts("  KTOS Button Control (Arduino Due)\r\n");
        uart_puts("=============================\r\n");
        uart_puts("Press the button on D2 (PB25)...\r\n");
        return MSG_WAIT;
    }
    if (MsgType == MSG_BUTTON_EVENT) {
        uart_puts(sParam ? "Button pressed\r\n" : "Button released\r\n");
    }
    return MSG_WAIT;
}

static bool     g_stable_pressed = false;
static bool     g_last_raw_high  = true;
static uint16_t g_debounce_count = 0;

static WORD button_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam;
    (void)lParam;

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

extern "C" void setup(void)
{
    WDT->WDT_MR = WDT_MR_WDDIS;
    uart_init();
    g_ui_task = ktos_InitTask(ui_task,    256, 4, 'I');
    ktos_InitTask(button_task,             256, 4, 'B');
    ktos_RunOS();
}

extern "C" void loop(void) { }
