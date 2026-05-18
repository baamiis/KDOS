/*
 * KTOS - Arduino Due LED control example
 *
 * Two cooperating KTOS tasks:
 *
 *   uart_task ('U')  -- polls UART RX every 20 ms, builds a line in a
 *                       fixed 16-byte buffer, parses ON/OFF/BLINK, and
 *                       posts MSG_SET_MODE to led_task.
 *
 *   led_task  ('L')  -- owns D13 (PB27).  On MSG_SET_MODE it switches
 *                       between OFF / ON / BLINK.  In BLINK mode it
 *                       returns 500 ms to toggle the LED on every
 *                       KTOS_MSG_TYPE_TIMER; in ON / OFF it returns
 *                       MSG_WAIT and stays idle until the next msg.
 *
 * No Arduino API calls - direct register access for UART, PIOB.
 */

#include "sam.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

extern "C" {
#include "../../../../../core/ktos.h"
}

/* =========================================================================
 * Programming Port UART - 115200 8N1 at 84 MHz
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

static int16_t uart_get_byte(void)
{
    if (!(UART->UART_SR & UART_SR_RXRDY)) { return -1; }
    return (int16_t)(UART->UART_RHR & 0xFFu);
}

/* =========================================================================
 * LED on D13 = PB27 (Arduino Due)
 * ========================================================================= */

#define LED_BIT_MASK   (1u << 27)

static inline void led_init(void)
{
    PMC->PMC_PCER0 = (1u << ID_PIOB);            /* clock the PIOB controller */
    PIOB->PIO_PER  = LED_BIT_MASK;               /* PIO control (vs peripheral) */
    PIOB->PIO_OER  = LED_BIT_MASK;               /* output enable */
    PIOB->PIO_CODR = LED_BIT_MASK;               /* clear -> low */
}

static inline void led_on(void)     { PIOB->PIO_SODR = LED_BIT_MASK; }
static inline void led_off(void)    { PIOB->PIO_CODR = LED_BIT_MASK; }
static inline bool led_is_on(void)  { return (PIOB->PIO_ODSR & LED_BIT_MASK) != 0; }
static inline void led_toggle(void) { if (led_is_on()) { led_off(); } else { led_on(); } }

/* =========================================================================
 * KTOS platform callbacks + tick ISR
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
 * Application message types
 * ========================================================================= */

#define MSG_SET_MODE ((WORD)(KTOS_MSG_TYPE_SYSTEM_START + 1))

#define MODE_OFF    0
#define MODE_ON     1
#define MODE_BLINK  2

#define BLINK_PERIOD_MS 500U
#define UART_POLL_MS    20U
#define CMD_BUF_SIZE    16U

static struct ktos_TASK *g_led_task = NULL;

static uint8_t g_led_mode  = MODE_BLINK;
static bool    g_led_level = false;

/* =========================================================================
 * LED task
 * ========================================================================= */

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
            return MSG_WAIT;

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
 * UART task
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
        uart_puts("  KTOS LED Control (Arduino Due)\r\n");
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
 * Arduino entry
 * ========================================================================= */

extern "C" void setup(void)
{
    WDT->WDT_MR = WDT_MR_WDDIS;
    uart_init();
    led_init();

    g_led_task = ktos_InitTask(led_task,  /* StackSize = */ 256,
                                          /* QueueSize = */ 4,
                                          /* TaskID    = */ 'L');
    ktos_InitTask(uart_task,              /* StackSize = */ 256,
                                          /* QueueSize = */ 4,
                                          /* TaskID    = */ 'U');
    ktos_RunOS();
}

extern "C" void loop(void) { }
