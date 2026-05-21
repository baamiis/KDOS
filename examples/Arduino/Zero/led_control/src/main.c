/*
 * KTOS — Arduino Zero LED control example (bare-metal)
 *
 * Two cooperating KTOS tasks:
 *   uart_task ('U') -- polls SERCOM5 RX every 20 ms, parses ON/OFF/BLINK,
 *                      posts MSG_SET_MODE to led_task.
 *   led_task  ('L') -- owns PA17 (D13, on-board LED). OFF / ON / BLINK.
 *
 * Serial: PROGRAMMING port (near RESET) via EDBG → SERCOM5 (PA22/PA23).
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "../../../../../core/ktos.h"

/* PM / GCLK */
#define PM_APBCMASK  (*(volatile uint32_t *)0x40000420UL)
#define GCLK_CLKCTRL (*(volatile uint16_t *)0x40000C02UL)
#define GCLK_STATUS  (*(volatile uint8_t  *)0x40000C01UL)

/* PORT A */
#define PORTA_DIRSET   (*(volatile uint32_t *)0x41004408UL)
#define PORTA_OUTSET   (*(volatile uint32_t *)0x41004418UL)
#define PORTA_OUTCLR   (*(volatile uint32_t *)0x41004414UL)
#define PORTA_OUTTGL   (*(volatile uint32_t *)0x4100441CUL)
#define PORTA_PMUX11   (*(volatile uint8_t  *)0x4100443BUL)
#define PORTA_PINCFG22 (*(volatile uint8_t  *)0x41004456UL)
#define PORTA_PINCFG23 (*(volatile uint8_t  *)0x41004457UL)

#define LED_BIT (1u << 17)  /* PA17 = D13 = on-board LED */

/* SERCOM5 USART */
#define SERCOM5_CTRLA    (*(volatile uint32_t *)0x42001C00UL)
#define SERCOM5_CTRLB    (*(volatile uint32_t *)0x42001C04UL)
#define SERCOM5_BAUD     (*(volatile uint16_t *)0x42001C0CUL)
#define SERCOM5_INTFLAG  (*(volatile uint8_t  *)0x42001C18UL)
#define SERCOM5_SYNCBUSY (*(volatile uint32_t *)0x42001C1CUL)
#define SERCOM5_DATA     (*(volatile uint16_t *)0x42001C28UL)

#define DRE_FLAG (1u << 0)
#define RXC_FLAG (1u << 2)

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
    SERCOM5_BAUD  = 63019u;
    SERCOM5_CTRLA |= (1u << 1);
    while (SERCOM5_SYNCBUSY & (1u << 1)) {}
}

static void uart_putc(char c) { while (!(SERCOM5_INTFLAG & DRE_FLAG)) {} SERCOM5_DATA = (uint8_t)c; }
static void uart_puts(const char *s) { while (*s) { uart_putc(*s++); } }
static int  uart_getc(void) { return (SERCOM5_INTFLAG & RXC_FLAG) ? (int)(SERCOM5_DATA & 0xFFu) : -1; }

static void led_init(void) { PORTA_DIRSET = LED_BIT; PORTA_OUTCLR = LED_BIT; }
static void led_on(void)   { PORTA_OUTSET = LED_BIT; }
static void led_off(void)  { PORTA_OUTCLR = LED_BIT; }

/* TC3 tick */
void TC3_Handler(void) { *(volatile uint8_t *)0x42002C0EUL = 1u; ktos_timer_irq_handler(); }

__attribute__((noreturn))
void ktos_Emergency(const char *msg) { uart_puts("\r\n[KTOS FATAL] "); uart_puts(msg); while (1) {} }
void ktos_DebugPrintf(const char *f, ...) { (void)f; }
void ktos_InitSys(void) {}

#define MSG_SET_MODE    ((WORD)(KTOS_MSG_TYPE_SYSTEM_START + 1))
#define MODE_OFF        0u
#define MODE_ON         1u
#define MODE_BLINK      2u
#define BLINK_PERIOD_MS 500U
#define UART_POLL_MS    20U
#define CMD_BUF_SIZE    16U

static struct ktos_TASK *g_led_task = NULL;
static uint8_t g_mode  = MODE_BLINK;
static bool    g_level = false;

static WORD led_task(WORD MsgType, WORD Param1, LONG Param2)
{
    (void)Param2;
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
            return KTOS_MSG_SLEEP_INDEFINITLY;
        case MSG_SET_MODE:
            g_mode = (uint8_t)Param1;
            if      (g_mode == MODE_ON)  { led_on();  uart_puts("LED ON\r\n");    return KTOS_MSG_SLEEP_INDEFINITLY; }
            else if (g_mode == MODE_OFF) { led_off(); uart_puts("LED OFF\r\n");   return KTOS_MSG_SLEEP_INDEFINITLY; }
            else { g_mode = MODE_BLINK; uart_puts("LED BLINK\r\n"); return BLINK_PERIOD_MS; }
    }
    return KTOS_MSG_SLEEP_INDEFINITLY;
}

static char    g_cmd_buf[CMD_BUF_SIZE];
static uint8_t g_cmd_len = 0;

static void dispatch(const char *line)
{
    if      (strcmp(line, "ON")    == 0) { ktos_SendMsg(g_led_task, MSG_SET_MODE, MODE_ON,    0); }
    else if (strcmp(line, "OFF")   == 0) { ktos_SendMsg(g_led_task, MSG_SET_MODE, MODE_OFF,   0); }
    else if (strcmp(line, "BLINK") == 0) { ktos_SendMsg(g_led_task, MSG_SET_MODE, MODE_BLINK, 0); }
    else if (line[0])                    { uart_puts("Unknown. Try ON, OFF, BLINK.\r\n"); }
}

static WORD uart_task(WORD MsgType, WORD Param1, LONG Param2)
{
    (void)Param1; (void)Param2;
    if (MsgType == KTOS_MSG_TYPE_INIT) {
        uart_puts("=============================\r\n");
        uart_puts("  KTOS LED Control\r\n");
        uart_puts("  Arduino Zero (SAMD21G18)\r\n");
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
        if (g_cmd_len < CMD_BUF_SIZE - 1u) { g_cmd_buf[g_cmd_len++] = up; }
    }
    return UART_POLL_MS;
}

int main(void)
{
    sercom5_init();
    g_led_task = ktos_InitTask(led_task,  128, 4, 'L');
    ktos_InitTask(uart_task, 128, 4, 'U');
    ktos_RunOS();
    return 0;
}
