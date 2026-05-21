/**
 * @file main.c
 * @brief KTOS WiFi connect demo for ESP8266 (Xtensa LX106).
 *
 * Execution flow:
 *   1. startup.c (windowed ABI) sets STATION_MODE, loads credentials from
 *      wifi_config.h, registers a WiFi event handler, then calls
 *      wifi_station_connect() once RF is ready.
 *   2. When EVENT_STAMODE_GOT_IP fires, on_wifi_event() saves IP/mask/gw/RSSI
 *      and crosses into CALL0 via callx0 -> ktos_app_main().
 *   3. ktos_app_main() creates connect_task and posts MSG_CONNECTED.
 *   4. connect_task receives MSG_CONNECTED, prints connection info, sleeps forever.
 *
 * Edit wifi_config.h to set your SSID and password before flashing.
 *
 * Expected output (74880 8N1):
 * @code
 * =============================
 *   KTOS WiFi Connect
 * =============================
 * SSID     : YourSSID
 * IP       : 192.168.1.42
 * Gateway  : 192.168.1.1
 * Mask     : 255.255.255.0
 * RSSI     : -67 dBm
 * ----
 * Connected.
 * @endcode
 */

#include "../../../core/ktos.h"
#include <stdint.h>
#include "wifi_config.h"

#define MSG_CONNECTED  ((WORD)(KTOS_MSG_TYPE_SYSTEM_START))

/* Set by startup.c on_wifi_event() before calling ktos_app_main(). */
extern uint32_t g_conn_ip;
extern uint32_t g_conn_gw;
extern uint32_t g_conn_mask;
extern int8_t   g_conn_rssi;

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

static void uart_puti(int32_t n)
{
    if (n < 0) { uart_putc('-'); uart_putu((uint32_t)(-n)); }
    else        { uart_putu((uint32_t)n); }
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
 * connect_task — waits for MSG_CONNECTED, prints connection info
 * ========================================================================= */

WORD connect_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam;
    (void)lParam;

    if (MsgType == KTOS_MSG_TYPE_INIT) return KTOS_MSG_SLEEP_INDEFINITLY;
    if (MsgType != MSG_CONNECTED)      return KTOS_MSG_SLEEP_INDEFINITLY;

    delay_ms(50);
    uart_puts("\033[2J\033[H");

    uart_puts("=============================\r\n");
    uart_puts("   KTOS WiFi Connect\r\n");
    uart_puts("=============================\r\n");

    uart_puts("SSID     : " WIFI_SSID "\r\n");

    uart_puts("IP       : ");
    uart_putip(g_conn_ip);
    uart_puts("\r\n");

    uart_puts("Gateway  : ");
    uart_putip(g_conn_gw);
    uart_puts("\r\n");

    uart_puts("Mask     : ");
    uart_putip(g_conn_mask);
    uart_puts("\r\n");

    uart_puts("RSSI     : ");
    uart_puti((int32_t)g_conn_rssi);
    uart_puts(" dBm\r\n");

    uart_puts("----\r\n");
    uart_puts("Connected.\r\n");

    return KTOS_MSG_SLEEP_INDEFINITLY;
}

/* =========================================================================
 * CALL0 application entry point — called from startup.c via callx0
 * ========================================================================= */

void ktos_app_main(void)
{
    struct ktos_TASK *conn = ktos_InitTask(connect_task, TASK_MAIN_STACK_SIZE,
                                            TASK_MAIN_QUEUE_SIZE, 'C');
    ktos_SendMsg(conn, MSG_CONNECTED, 0, 0);

    ktos_RunOS();
}
