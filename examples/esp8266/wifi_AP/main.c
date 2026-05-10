/**
 * @file main.c
 * @brief KTOS WiFi AP demo for ESP8266 (Xtensa LX106).
 *
 * Execution flow:
 *   1. startup.c (windowed ABI) sets SOFTAP_MODE, configures AP from
 *      wifi_ap_config.h, waits for RF ready via system_init_done_cb.
 *   2. on_sdk_ready() reads the AP IP, then callx0 -> ktos_app_main().
 *   3. ktos_app_main() creates ap_task and posts MSG_AP_READY.
 *   4. ap_task receives MSG_AP_READY, prints AP config and IP, sleeps forever.
 *
 * Expected output (74880 8N1):
 * @code
 * =============================
 *   KTOS WiFi AP
 * =============================
 * SSID     : KTOS-AP
 * Password : ktos1234
 * Channel  : 6
 * Auth     : WPA2
 * IP       : 192.168.4.1
 * ----
 * AP ready - connect to KTOS-AP
 * @endcode
 */

#include "../../../core/ktos.h"
#include <stdint.h>
#include "wifi_ap_config.h"

#define MSG_AP_READY  ((WORD)(KTOS_MSG_TYPE_SYSTEM_START))

/* Set by startup.c on_sdk_ready() before calling ktos_app_main(). */
extern uint32_t g_ap_ip;

/* =========================================================================
 * UART0 — direct register access, 74880 8N1 (inherited from SDK)
 * ========================================================================= */

#define UART0_FIFO   (*(volatile uint32_t *)0x60000000UL)
#define UART0_STATUS (*(volatile uint32_t *)0x6000001CUL)

static void uart_putc(char c)
{
    while (((UART0_STATUS >> 16) & 0xFFu) >= 126u);
    UART0_FIFO = (uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s) uart_putc(*s++);
}

static void uart_putu(uint32_t n)
{
    char buf[10];
    int i = 0;
    if (n == 0) { uart_putc('0'); return; }
    while (n) { buf[i++] = '0' + (char)(n % 10); n /= 10; }
    while (i--) uart_putc(buf[i]);
}

/* Print a uint32_t IP address stored in network-byte-order as dotted-quad. */
static void uart_putip(uint32_t ip)
{
    uint8_t *b = (uint8_t *)&ip;
    uart_putu(b[0]); uart_putc('.');
    uart_putu(b[1]); uart_putc('.');
    uart_putu(b[2]); uart_putc('.');
    uart_putu(b[3]);
}

/* =========================================================================
 * Delay — Xtensa CCOUNT at 80 MHz
 * ========================================================================= */

#define CPU_MHZ 80UL

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
 * ap_task — waits for MSG_AP_READY, prints AP info, sleeps forever
 * ========================================================================= */

WORD ap_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam;
    (void)lParam;

    if (MsgType == KTOS_MSG_TYPE_INIT) return MSG_WAIT;
    if (MsgType != MSG_AP_READY)       return MSG_WAIT;

    delay_ms(50);
    uart_puts("\033[2J\033[H");

    uart_puts("=============================\r\n");
    uart_puts("   KTOS WiFi AP\r\n");
    uart_puts("=============================\r\n");

    uart_puts("SSID     : " AP_SSID "\r\n");
    uart_puts("Password : " AP_PASSWORD "\r\n");
    uart_puts("Channel  : ");
    uart_putu(AP_CHANNEL);
    uart_puts("\r\n");
    uart_puts("Auth     : WPA2\r\n");
    uart_puts("IP       : ");
    uart_putip(g_ap_ip);
    uart_puts("\r\n");

    uart_puts("----\r\n");
    uart_puts("AP ready - connect to " AP_SSID "\r\n");

    /* Signal the scheduler to return from ktos_RunOS().  on_sdk_ready()
     * in startup.c will then return to ets_run(), keeping the AP alive. */
    ktos_ExitOS();
    return MSG_WAIT;
}

/* =========================================================================
 * CALL0 application entry point — called from startup.c via callx0
 * ========================================================================= */

void ktos_app_main(void)
{
    struct ktos_TASK *ap = ktos_InitTask(ap_task, TASK_MAIN_STACK_SIZE,
                                          TASK_MAIN_QUEUE_SIZE, 'A');
    ktos_SendMsg(ap, MSG_AP_READY, 0, 0);

    ktos_RunOS();
}
