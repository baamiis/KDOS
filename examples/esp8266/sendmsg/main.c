/**
 * @file main.c
 * @brief KTOS two-task ktos_SendMsg() demo for ESP8266 (Xtensa LX106).
 *
 * ping_task sends MSG_PING to pong_task; pong_task replies with MSG_PONG.
 * Each task is a plain message handler — it receives one message per call
 * and returns KTOS_MSG_SLEEP_INDEFINITLY to sleep until the next one arrives.
 *
 * Expected serial output (74880 8N1):
 * @code
 * =============================
 *   KTOS SendMsg demo
 * =============================
 * [PING] sending ping #1
 * [PONG] got ping #1 -- sending pong
 * [PING] got pong -- sending ping #2
 * [PONG] got ping #2 -- sending pong
 * ...
 * @endcode
 */

#include "../../../core/ktos.h"
#include <stdint.h>

/* =========================================================================
 * UART0 — direct register access, 74880 8N1 at ~52 MHz
 * ========================================================================= */

#define UART0_FIFO   (*(volatile uint32_t *)0x60000000UL)
#define UART0_STATUS (*(volatile uint32_t *)0x6000001CUL)
#define UART0_CONF0  (*(volatile uint32_t *)0x60000020UL)
#define UART0_CLKDIV (*(volatile uint32_t *)0x60000014UL)

/* 74880 baud: 52 000 000 / 74880 ≈ 694 (ROM PLL sets ~52 MHz before user code) */
#define UART_BAUD_DIV 694u

static void uart_init(void)
{
    UART0_CLKDIV = UART_BAUD_DIV;
    UART0_CONF0  = 0x1cUL;
}

static void uart_putc(char c)
{
    while (((UART0_STATUS >> 16) & 0xFFu) >= 126u);
    UART0_FIFO = (uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s) uart_putc(*s++);
}

static void uart_putu16(uint16_t n)
{
    char buf[6];
    int i = 0;
    if (n == 0) { uart_putc('0'); return; }
    while (n) { buf[i++] = '0' + (char)(n % 10); n /= 10; }
    while (i--) uart_putc(buf[i]);
}

/* =========================================================================
 * Delay — Xtensa CCOUNT at ~52 MHz
 * ========================================================================= */

#define CPU_MHZ 52UL

static void delay_ms(uint32_t ms)
{
    uint32_t t0, tn;
    __asm__ volatile ("rsr %0, CCOUNT" : "=r"(t0));
    do {
        __asm__ volatile ("rsr %0, CCOUNT" : "=r"(tn));
    } while (tn - t0 < ms * (CPU_MHZ * 1000UL));
}

/* =========================================================================
 * Required KTOS platform callbacks
 * ========================================================================= */

__attribute__((noreturn)) void ktos_Emergency(const char *msg)
{
    uart_puts("\r\n[KTOS FATAL] ");
    uart_puts(msg);
    uart_puts("\r\n");
    while (1);
}

void ktos_DebugPrintf(const char *fmt, ...) { (void)fmt; }
void ktos_InitSys(void) {}

/* =========================================================================
 * User-defined message types (values must be > KTOS_MSG_TYPE_SYSTEM_START)
 * ========================================================================= */

#define MSG_PING (KTOS_MSG_TYPE_SYSTEM_START + 1)
#define MSG_PONG (KTOS_MSG_TYPE_SYSTEM_START + 2)

static struct ktos_TASK *g_ping_task;
static struct ktos_TASK *g_pong_task;

/* =========================================================================
 * Ping task — initiates the exchange, waits 1 s between rounds
 * ========================================================================= */

static uint16_t g_count;

WORD ping_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam;
    (void)lParam;

    switch (MsgType) {
        case KTOS_MSG_TYPE_INIT:
            delay_ms(50);
            uart_puts("\033[2J\033[H");   /* clear ROM garbage from terminal */
            uart_puts("=============================\r\n");
            uart_puts("   KTOS SendMsg demo\r\n");
            uart_puts("=============================\r\n");
            g_count = 1;
            uart_puts("[PING] sending ping #1\r\n");
            ktos_SendMsg(g_pong_task, MSG_PING, g_count, 0);
            break;

        case MSG_PONG:
            delay_ms(1000);
            ++g_count;
            uart_puts("[PING] got pong -- sending ping #");
            uart_putu16(g_count);
            uart_puts("\r\n");
            ktos_SendMsg(g_pong_task, MSG_PING, g_count, 0);
            break;
    }

    return KTOS_MSG_SLEEP_INDEFINITLY;
}

/* =========================================================================
 * Pong task — echoes each ping back as a pong
 * ========================================================================= */

WORD pong_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)lParam;

    if (MsgType == MSG_PING) {
        uart_puts("[PONG] got ping #");
        uart_putu16(sParam);
        uart_puts(" -- sending pong\r\n");
        ktos_SendMsg(g_ping_task, MSG_PONG, 0, 0);
    }

    return KTOS_MSG_SLEEP_INDEFINITLY;
}

/* =========================================================================
 * CALL0 application entry point
 * ========================================================================= */

void ktos_app_main(void)
{
    *(volatile uint32_t *)0x60000900UL = 0;   /* disable hardware watchdog */

    uart_init();

    g_ping_task = ktos_InitTask(ping_task, TASK_MAIN_STACK_SIZE, TASK_MAIN_QUEUE_SIZE, 'P');
    g_pong_task = ktos_InitTask(pong_task, TASK_MAIN_STACK_SIZE, TASK_MAIN_QUEUE_SIZE, 'O');

    ktos_RunOS();   /* never returns */
}
