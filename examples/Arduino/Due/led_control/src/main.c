/*
 * KTOS — Arduino Due LED control example (bare-metal)
 *
 * Two cooperating KTOS tasks:
 *   uart_task ('U') -- polls UART0 RX every 20 ms, parses ON/OFF/BLINK,
 *                      posts MSG_SET_MODE to led_task.
 *   led_task  ('L') -- owns D13 (PB27). Switches OFF / ON / BLINK.
 *
 * D13 on the Due = PB27 (differs from UNO/Nano PB5).
 * Serial: connect via the PROGRAMMING port (small USB near reset).
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "../../../../../core/ktos.h"

/* PMC */
#define PMC_PCER0  (*(volatile uint32_t *)0x400E0610UL)

/* PIOA — UART0 pins */
#define PIOA_PDR   (*(volatile uint32_t *)0x400E0E04UL)
#define PIOA_ABSR  (*(volatile uint32_t *)0x400E0E70UL)

/* PIOB — LED PB27 */
#define PIOB_PER   (*(volatile uint32_t *)0x400E1000UL)
#define PIOB_OER   (*(volatile uint32_t *)0x400E1010UL)
#define PIOB_SODR  (*(volatile uint32_t *)0x400E1030UL)
#define PIOB_CODR  (*(volatile uint32_t *)0x400E1034UL)
#define PIOB_ODSR  (*(volatile uint32_t *)0x400E1038UL)
#define LED_BIT    (1u << 27)

/* UART0 */
#define UART0_CR   (*(volatile uint32_t *)0x400E0800UL)
#define UART0_MR   (*(volatile uint32_t *)0x400E0804UL)
#define UART0_SR   (*(volatile uint32_t *)0x400E0814UL)
#define UART0_RHR  (*(volatile uint32_t *)0x400E0818UL)
#define UART0_THR  (*(volatile uint32_t *)0x400E081CUL)
#define UART0_BRGR (*(volatile uint32_t *)0x400E0820UL)
#define UART_SR_RXRDY (1u << 0)
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
static int  uart_getc(void) { return (UART0_SR & UART_SR_RXRDY) ? (int)(UART0_RHR & 0xFF) : -1; }

static void led_init(void)
{
    PMC_PCER0 |= (1u << 12);   /* PIOB clock (ID 12) */
    PIOB_PER   = LED_BIT;
    PIOB_OER   = LED_BIT;
    PIOB_CODR  = LED_BIT;
}
static void led_on(void)     { PIOB_SODR = LED_BIT; }
static void led_off(void)    { PIOB_CODR = LED_BIT; }

/* TC0 channel 0 — KTOS tick */
#define TC0_CH0_SR (*(volatile uint32_t *)0x40080020UL)
void TC0_Handler(void) { (void)TC0_CH0_SR; ktos_timer_irq_handler(); }

__attribute__((noreturn))
void ktos_Emergency(const char *msg) { uart_puts("\r\n[KTOS FATAL] "); uart_puts(msg); while (1) {} }
void ktos_DebugPrintf(const char *f, ...) { (void)f; }
void ktos_InitSys(void) {}

/* Message types */
#define MSG_SET_MODE    ((WORD)(KTOS_MSG_TYPE_SYSTEM_START + 1))
#define MODE_OFF        0
#define MODE_ON         1
#define MODE_BLINK      2
#define BLINK_PERIOD_MS 500U
#define UART_POLL_MS    20U
#define CMD_BUF_SIZE    16U

static struct ktos_TASK *g_led_task = NULL;

/* LED task */
static uint8_t g_mode  = MODE_BLINK;
static bool    g_level = false;

static WORD led_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)lParam;
    switch (MsgType) {
        case KTOS_MSG_TYPE_INIT:
            led_init(); led_off();
            uart_puts("LED BLINK\r\n");
            g_mode = MODE_BLINK;
            return BLINK_PERIOD_MS;
        case KTOS_MSG_TYPE_TIMER:
            if (g_mode == MODE_BLINK) {
                g_level = !g_level;
                if (g_level) { led_on(); } else { led_off(); }
                return BLINK_PERIOD_MS;
            }
            return MSG_WAIT;
        case MSG_SET_MODE:
            g_mode = (uint8_t)sParam;
            if      (g_mode == MODE_ON)  { led_on();  uart_puts("LED ON\r\n");    return MSG_WAIT; }
            else if (g_mode == MODE_OFF) { led_off(); uart_puts("LED OFF\r\n");   return MSG_WAIT; }
            else                         { g_mode = MODE_BLINK; uart_puts("LED BLINK\r\n"); return BLINK_PERIOD_MS; }
    }
    return MSG_WAIT;
}

/* UART task */
static char    g_cmd_buf[CMD_BUF_SIZE];
static uint8_t g_cmd_len = 0;

static void dispatch(const char *line)
{
    if      (strcmp(line, "ON")    == 0) { ktos_SendMsg(g_led_task, MSG_SET_MODE, MODE_ON,    0); }
    else if (strcmp(line, "OFF")   == 0) { ktos_SendMsg(g_led_task, MSG_SET_MODE, MODE_OFF,   0); }
    else if (strcmp(line, "BLINK") == 0) { ktos_SendMsg(g_led_task, MSG_SET_MODE, MODE_BLINK, 0); }
    else if (line[0])                    { uart_puts("Unknown. Try ON, OFF, BLINK.\r\n"); }
}

static WORD uart_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam; (void)lParam;
    if (MsgType == KTOS_MSG_TYPE_INIT) {
        uart_puts("=============================\r\n");
        uart_puts("  KTOS LED Control\r\n");
        uart_puts("  Arduino Due (SAM3X8E)\r\n");
        uart_puts("=============================\r\n");
        uart_puts("Commands: ON, OFF, BLINK\r\n");
        return UART_POLL_MS;
    }
    int b;
    while ((b = uart_getc()) >= 0) {
        char c = (char)b;
        if (c == '\r' || c == '\n') {
            if (g_cmd_len > 0) { g_cmd_buf[g_cmd_len] = '\0'; dispatch(g_cmd_buf); g_cmd_len = 0; }
            continue;
        }
        char up = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
        if (g_cmd_len < CMD_BUF_SIZE - 1) { g_cmd_buf[g_cmd_len++] = up; }
    }
    return UART_POLL_MS;
}

int main(void)
{
    uart_init();
    g_led_task = ktos_InitTask(led_task,  96, 4, 'L');
    ktos_InitTask(uart_task, 96, 4, 'U');
    ktos_RunOS();
    return 0;
}
