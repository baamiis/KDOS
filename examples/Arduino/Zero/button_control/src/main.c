/*
 * KTOS — Arduino Zero button control example (bare-metal)
 *
 * Two cooperating KTOS tasks:
 *   button_task ('B') -- polls D2 (PA14) every 5 ms, 30 ms debounce,
 *                        posts MSG_BUTTON_EVENT to ui_task on each edge.
 *   ui_task     ('I') -- prints "Button pressed" / "Button released".
 *
 * D2 on the Zero = PA14.  Connect button between D2 and GND.
 * Internal pull-up enabled via PORT_PINCFG.PULLEN + PORT_OUT high.
 * Serial: PROGRAMMING port via EDBG → SERCOM5 (PA22=TX, PA23=RX).
 */

#include <stdint.h>
#include <stdbool.h>
#include "../../../../../core/ktos.h"

/* PM / GCLK */
#define PM_APBCMASK  (*(volatile uint32_t *)0x40000420UL)
#define GCLK_CLKCTRL (*(volatile uint16_t *)0x40000C02UL)
#define GCLK_STATUS  (*(volatile uint8_t  *)0x40000C01UL)

/* PORT A */
#define PORTA_DIRSET   (*(volatile uint32_t *)0x41004408UL)
#define PORTA_DIRCLR   (*(volatile uint32_t *)0x41004404UL)
#define PORTA_OUTSET   (*(volatile uint32_t *)0x41004418UL)
#define PORTA_IN       (*(volatile uint32_t *)0x41004420UL)
#define PORTA_PMUX11   (*(volatile uint8_t  *)0x4100443BUL)
#define PORTA_PINCFG14 (*(volatile uint8_t  *)0x4100444EUL)
#define PORTA_PINCFG22 (*(volatile uint8_t  *)0x41004456UL)
#define PORTA_PINCFG23 (*(volatile uint8_t  *)0x41004457UL)

#define BTN_BIT (1u << 14)  /* PA14 = D2 */

/* SERCOM5 */
#define SERCOM5_CTRLA    (*(volatile uint32_t *)0x42001C00UL)
#define SERCOM5_CTRLB    (*(volatile uint32_t *)0x42001C04UL)
#define SERCOM5_BAUD     (*(volatile uint16_t *)0x42001C0CUL)
#define SERCOM5_INTFLAG  (*(volatile uint8_t  *)0x42001C18UL)
#define SERCOM5_SYNCBUSY (*(volatile uint32_t *)0x42001C1CUL)
#define SERCOM5_DATA     (*(volatile uint16_t *)0x42001C28UL)
#define DRE_FLAG (1u << 0)

static void sercom5_init(void)
{
    PM_APBCMASK |= (1u << 7);
    GCLK_CLKCTRL = (uint16_t)(25u | (0u << 8) | (1u << 14));
    while (GCLK_STATUS & (1u << 7)) {}
    PORTA_PMUX11   = (3u << 4) | 3u;
    PORTA_PINCFG22 = (1u << 0);
    PORTA_PINCFG23 = (1u << 0) | (1u << 1);
    SERCOM5_CTRLA = (1u << 0);
    while (SERCOM5_SYNCBUSY & (1u << 0)) {}
    SERCOM5_CTRLA = (0x4u) | (1u << 20) | (1u << 28);
    SERCOM5_CTRLB = (1u << 16) | (1u << 17);
    while (SERCOM5_SYNCBUSY & (1u << 2)) {}
    SERCOM5_BAUD = 63019u;
    SERCOM5_CTRLA |= (1u << 1);
    while (SERCOM5_SYNCBUSY & (1u << 1)) {}
}

static void uart_putc(char c) { while (!(SERCOM5_INTFLAG & DRE_FLAG)) {} SERCOM5_DATA = (uint8_t)c; }
static void uart_puts(const char *s) { while (*s) { uart_putc(*s++); } }

static void btn_init(void)
{
    PORTA_DIRCLR   = BTN_BIT;               /* input */
    PORTA_PINCFG14 = (1u << 1) | (1u << 2); /* INEN | PULLEN */
    PORTA_OUTSET   = BTN_BIT;               /* pull-up (OUT=1 with PULLEN) */
}
static bool btn_high(void) { return (PORTA_IN & BTN_BIT) != 0u; }

/* TC3 tick */
void TC3_Handler(void) { *(volatile uint8_t *)0x42002C0EUL = 1u; ktos_timer_irq_handler(); }

__attribute__((noreturn))
void ktos_Emergency(const char *msg) { uart_puts("\r\n[KTOS FATAL] "); uart_puts(msg); while (1) {} }
void ktos_DebugPrintf(const char *f, ...) { (void)f; }
void ktos_InitSys(void) {}

#define MSG_BUTTON_EVENT ((WORD)(KTOS_MSG_TYPE_SYSTEM_START + 1))
#define POLL_MS     5U
#define DEBOUNCE_MS 30U

static struct ktos_TASK *g_ui_task = NULL;

static WORD ui_task(WORD MsgType, WORD Param1, LONG Param2)
{
    (void)Param2;
    if (MsgType == KTOS_MSG_TYPE_INIT) {
        uart_puts("=============================\r\n");
        uart_puts("  KTOS Button Control\r\n");
        uart_puts("  Arduino Zero (SAMD21G18)\r\n");
        uart_puts("  Connect button: D2 (PA14) to GND\r\n");
        uart_puts("=============================\r\n");
        return KTOS_MSG_SLEEP_INDEFINITLY;
    }
    if (MsgType == MSG_BUTTON_EVENT) {
        uart_puts(Param1 ? "Button pressed\r\n" : "Button released\r\n");
    }
    return KTOS_MSG_SLEEP_INDEFINITLY;
}

static bool     g_stable   = false;
static bool     g_last_hi  = true;
static uint16_t g_debounce = 0;

static WORD button_task(WORD MsgType, WORD Param1, LONG Param2)
{
    (void)Param1; (void)Param2;
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
    sercom5_init();
    g_ui_task = ktos_InitTask(ui_task,     128, 4, 'I');
    ktos_InitTask(button_task, 96, 2, 'B');
    ktos_RunOS();
    return 0;
}
