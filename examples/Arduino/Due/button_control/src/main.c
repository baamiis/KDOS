/*
 * KTOS — Arduino Due button control example (bare-metal)
 *
 * Two cooperating KTOS tasks:
 *   button_task ('B') -- polls D2 (PB25) every 5 ms, 30 ms debounce,
 *                        posts MSG_BUTTON_EVENT to ui_task on each edge.
 *   ui_task     ('I') -- prints "Button pressed" / "Button released".
 *
 * D2 on the Due = PB25.  Connect button between D2 and GND.
 * Internal pull-up enabled via PIO_PUER.
 * Serial: connect via the PROGRAMMING port (small USB near reset).
 */

#include <stdint.h>
#include <stdbool.h>
#include "../../../../../core/ktos.h"

/* PMC */
#define PMC_PCER0  (*(volatile uint32_t *)0x400E0610UL)

/* PIOA — UART0 */
#define PIOA_PDR   (*(volatile uint32_t *)0x400E0E04UL)
#define PIOA_ABSR  (*(volatile uint32_t *)0x400E0E70UL)

/* PIOB — button PB25 */
#define PIOB_PER   (*(volatile uint32_t *)0x400E1000UL)
#define PIOB_PDSR  (*(volatile uint32_t *)0x400E103CUL)
#define PIOB_PUER  (*(volatile uint32_t *)0x400E1064UL)
#define BTN_BIT    (1u << 25)

/* UART0 */
#define UART0_CR   (*(volatile uint32_t *)0x400E0800UL)
#define UART0_MR   (*(volatile uint32_t *)0x400E0804UL)
#define UART0_SR   (*(volatile uint32_t *)0x400E0814UL)
#define UART0_THR  (*(volatile uint32_t *)0x400E081CUL)
#define UART0_BRGR (*(volatile uint32_t *)0x400E0820UL)
#define UART_SR_TXRDY (1u << 1)

static void uart_init(void)
{
    PMC_PCER0  = (1u << 11) | (1u << 8);
    PIOA_PDR   = (1u << 8) | (1u << 9);
    PIOA_ABSR &= ~((1u << 8) | (1u << 9));
    UART0_CR   = (1u << 2) | (1u << 3);
    UART0_MR   = (4u << 9);
    UART0_BRGR = 46u;
    UART0_CR   = (1u << 4) | (1u << 6);
}

static void uart_putc(char c) { while (!(UART0_SR & UART_SR_TXRDY)) {} UART0_THR = (uint8_t)c; }
static void uart_puts(const char *s) { while (*s) { uart_putc(*s++); } }

static void btn_init(void)
{
    PMC_PCER0 |= (1u << 12);   /* PIOB clock */
    PIOB_PER   = BTN_BIT;      /* GPIO mode (input by default) */
    PIOB_PUER  = BTN_BIT;      /* enable pull-up */
}

static bool btn_high(void) { return (PIOB_PDSR & BTN_BIT) != 0; }

/* TC0 — KTOS tick */
#define TC0_CH0_SR (*(volatile uint32_t *)0x40080020UL)
void TC0_Handler(void) { (void)TC0_CH0_SR; ktos_timer_irq_handler(); }

__attribute__((noreturn))
void ktos_Emergency(const char *msg) { uart_puts("\r\n[KTOS FATAL] "); uart_puts(msg); while (1) {} }
void ktos_DebugPrintf(const char *f, ...) { (void)f; }
void ktos_InitSys(void) {}

#define MSG_BUTTON_EVENT ((WORD)(KTOS_MSG_TYPE_SYSTEM_START + 1))
#define POLL_MS   5U
#define DEBOUNCE_MS 30U

static struct ktos_TASK *g_ui_task = NULL;

static WORD ui_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)lParam;
    if (MsgType == KTOS_MSG_TYPE_INIT) {
        uart_puts("=============================\r\n");
        uart_puts("  KTOS Button Control\r\n");
        uart_puts("  Arduino Due (SAM3X8E)\r\n");
        uart_puts("  Connect button: D2 (PB25) to GND\r\n");
        uart_puts("=============================\r\n");
        return MSG_WAIT;
    }
    if (MsgType == MSG_BUTTON_EVENT) {
        uart_puts(sParam ? "Button pressed\r\n" : "Button released\r\n");
    }
    return MSG_WAIT;
}

static bool     g_stable   = false;
static bool     g_last_hi  = true;
static uint16_t g_debounce = 0;

static WORD button_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam; (void)lParam;
    if (MsgType == KTOS_MSG_TYPE_INIT) {
        btn_init();
        g_last_hi  = btn_high();
        g_stable   = !g_last_hi;
        g_debounce = 0;
        return POLL_MS;
    }
    bool hi = btn_high();
    if (hi != g_last_hi) { g_last_hi = hi; g_debounce = 0; return POLL_MS; }

    const uint16_t needed = DEBOUNCE_MS / POLL_MS;
    if (g_debounce < needed) {
        if (++g_debounce == needed) {
            bool pressed = !hi;
            if (pressed != g_stable) {
                g_stable = pressed;
                ktos_SendMsg(g_ui_task, MSG_BUTTON_EVENT, pressed ? 1u : 0u, 0);
            }
        }
    }
    return POLL_MS;
}

int main(void)
{
    uart_init();
    g_ui_task = ktos_InitTask(ui_task,     96, 4, 'I');
    ktos_InitTask(button_task, 64, 2, 'B');
    ktos_RunOS();
    return 0;
}
