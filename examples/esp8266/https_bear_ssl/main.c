/**
 * @file main.c
 * @brief KTOS BearSSL TLS 1.2 GET demo for ESP8266 (Xtensa LX106).
 *
 * Execution flow:
 *   1. startup.c connects to WiFi, resolves DNS, opens a plain TCP connection.
 *   2. bearssl_conn.c performs the BearSSL TLS 1.2 handshake over that TCP
 *      connection, sends the HTTP GET, and accumulates the response.
 *   3. When the TLS session closes, do_exit() crosses into CALL0 via callx0.
 *   4. ktos_app_main() posts MSG_HTTPS_DONE to https_task.
 *   5. https_task prints the status line and response body.
 *
 * This example demonstrates that KTOS's smaller heap footprint (~4 KB vs
 * ~20 KB for a typical Arduino sketch) makes room for BearSSL's working
 * state (~13–15 KB), enabling genuine TLS 1.2 on servers that reject the
 * axTLS 1.0/1.1 stack bundled in the ESP8266 Non-OS SDK.
 *
 * Expected output (74880 8N1, httpbin.org/ip):
 * @code
 * ================================
 *   KTOS BearSSL TLS 1.2 GET
 * ================================
 * Host   : httpbin.org
 * Path   : /ip
 * Mode   : TLS 1.2 (BearSSL)
 * ----
 * Status : HTTP/1.1 200 OK
 * Body   :
 * {
 *   "origin": "203.0.113.42"
 * }
 * ----
 * Done. (256 bytes total)
 * @endcode
 */

#include "../../../core/ktos.h"
#include <stdint.h>
#include "http_config.h"

#define MSG_HTTPS_DONE  ((WORD)(KTOS_MSG_TYPE_SYSTEM_START))

extern char     g_resp_buf[];
extern uint16_t g_resp_len;
extern uint8_t  g_resp_err;
extern int      g_ssl_err;

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

static void uart_puts(const char *s)  { while (*s) uart_putc(*s++); }

static void uart_putu(uint32_t n)
{
    char buf[10]; int i = 0;
    if (n == 0) { uart_putc('0'); return; }
    while (n) { buf[i++] = '0' + (char)(n % 10); n /= 10; }
    while (i--) uart_putc(buf[i]);
}

static void uart_write(const char *buf, uint16_t len)
{
    while (len--) uart_putc(*buf++);
}

/* =========================================================================
 * Delay — Xtensa CCOUNT at 80 MHz
 * ========================================================================= */

#define CPU_MHZ 80UL

static void delay_ms(uint32_t ms)
{
    uint32_t t0, tn;
    __asm__ volatile ("rsr %0, CCOUNT" : "=r"(t0));
    do { __asm__ volatile ("rsr %0, CCOUNT" : "=r"(tn)); }
    while (tn - t0 < ms * (CPU_MHZ * 1000UL));
}

/* =========================================================================
 * Required KTOS platform callbacks
 * ========================================================================= */

__attribute__((noreturn)) void ktos_Emergency(const char *msg)
{
    uart_puts("\r\n[KTOS FATAL] "); uart_puts(msg); uart_puts("\r\n");
    while (1);
}
void ktos_DebugPrintf(const char *fmt, ...) { (void)fmt; }
void ktos_InitSys(void) {}

/* =========================================================================
 * Response helpers
 * ========================================================================= */

static const char *mem_find(const char *hay, uint16_t hlen,
                             const char *needle, uint16_t nlen)
{
    if (nlen == 0 || hlen < nlen) return NULL;
    uint16_t limit = (uint16_t)(hlen - nlen);
    for (uint16_t i = 0; i <= limit; i++) {
        if (hay[i] == needle[0]) {
            uint16_t j;
            for (j = 1; j < nlen; j++)
                if (hay[i + j] != needle[j]) break;
            if (j == nlen) return hay + i;
        }
    }
    return NULL;
}

static const char *print_line(const char *p, const char *end)
{
    const char *q = p;
    while (q < end && *q != '\r' && *q != '\n') uart_putc(*q++);
    uart_puts("\r\n");
    if (q < end && *q == '\r') q++;
    if (q < end && *q == '\n') q++;
    return q;
}

/* =========================================================================
 * https_task
 * ========================================================================= */

WORD https_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam; (void)lParam;

    if (MsgType == KTOS_MSG_TYPE_INIT) return KTOS_MSG_SLEEP_INDEFINITLY;
    if (MsgType != MSG_HTTPS_DONE)     return KTOS_MSG_SLEEP_INDEFINITLY;

    delay_ms(50);
    uart_puts("\033[2J\033[H");

    uart_puts("================================\r\n");
    uart_puts("   KTOS BearSSL TLS 1.2 GET\r\n");
    uart_puts("================================\r\n");
    uart_puts("Host   : " HTTP_HOST "\r\n");
    uart_puts("Path   : " HTTP_PATH "\r\n");
    uart_puts("Mode   : TLS 1.2 (BearSSL)\r\n");
    uart_puts("----\r\n");

    if (g_resp_err == 1) {
        uart_puts("Error  : DNS resolution failed for " HTTP_HOST "\r\n");
        return KTOS_MSG_SLEEP_INDEFINITLY;
    }
    if (g_resp_err == 2) {
        uart_puts("Error  : TLS 1.2 handshake or TCP connection failed.\r\n");
        uart_puts("         BearSSL err=");
        uart_putu((uint32_t)g_ssl_err);
        uart_puts("\r\n");
        return KTOS_MSG_SLEEP_INDEFINITLY;
    }

    if (g_resp_len == 0) {
        uart_puts("Error  : Empty response.\r\n");
        return KTOS_MSG_SLEEP_INDEFINITLY;
    }

    const char *buf = g_resp_buf;
    const char *end = buf + g_resp_len;

    uart_puts("Status : ");
    buf = print_line(buf, end);

    const char *body = mem_find(g_resp_buf, g_resp_len, "\r\n\r\n", 4);
    if (body) {
        body += 4;
        uint16_t body_len = (uint16_t)(end - body);
        uart_puts("Body   :\r\n");
        uart_write(body, body_len);
        if (body_len > 0 && body[body_len - 1] != '\n')
            uart_puts("\r\n");
    } else {
        uart_puts("Body   :\r\n");
        uart_write(g_resp_buf, g_resp_len);
        uart_puts("\r\n");
    }

    uart_puts("----\r\n");
    uart_puts("Done. (");
    uart_putu(g_resp_len);
    uart_puts(" bytes total)\r\n");

    return KTOS_MSG_SLEEP_INDEFINITLY;
}

/* =========================================================================
 * CALL0 application entry point — called from startup.c via callx0
 * ========================================================================= */

void ktos_app_main(void)
{
    struct ktos_TASK *t = ktos_InitTask(https_task, TASK_MAIN_STACK_SIZE,
                                         TASK_MAIN_QUEUE_SIZE, 'S');
    ktos_SendMsg(t, MSG_HTTPS_DONE, 0, 0);
    ktos_RunOS();
}
