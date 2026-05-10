/**
 * @file main.c
 * @brief KTOS HTTP(S) GET demo for ESP8266 (Xtensa LX106).
 *
 * Execution flow:
 *   1. startup.c (windowed ABI) connects to WiFi, resolves DNS, opens a
 *      TCP (or TLS) connection, sends a GET request, and accumulates the
 *      full HTTP response in g_resp_buf.
 *   2. On disconnect, startup.c stops FRC1, disables WDTs, and crosses
 *      into CALL0 via callx0 -> ktos_app_main().
 *   3. ktos_app_main() posts MSG_HTTP_DONE to http_task.
 *   4. http_task parses the response: prints the HTTP status line, then
 *      the response body.
 *
 * Expected output (74880 8N1, HTTP mode, httpbin.org/ip):
 * @code
 * =============================
 *   KTOS HTTP GET
 * =============================
 * Host   : httpbin.org
 * Path   : /ip
 * Mode   : HTTP
 * ----
 * Status : HTTP/1.1 200 OK
 * Body   :
 * {
 *   "origin": "203.0.113.42"
 * }
 * ----
 * Done.
 * @endcode
 */

#include "../../../core/ktos.h"
#include <stdint.h>
#include "http_config.h"

#define MSG_HTTP_DONE  ((WORD)(KTOS_MSG_TYPE_SYSTEM_START))

/* Written by startup.c, read here. */
extern char     g_resp_buf[];
extern uint16_t g_resp_len;
extern uint8_t  g_resp_err;

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

/* Print exactly `len` bytes from `buf` (may contain NUL bytes). */
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
 * HTTP response helpers
 * ========================================================================= */

/* Find the first occurrence of `needle` (length `nlen`) inside `hay`
 * (length `hlen`).  Returns pointer to match or NULL. */
static const char *mem_find(const char *hay, uint16_t hlen,
                             const char *needle, uint16_t nlen)
{
    if (nlen == 0 || hlen < nlen) return NULL;
    uint16_t limit = hlen - nlen;
    for (uint16_t i = 0; i <= limit; i++) {
        if (hay[i] == needle[0]) {
            uint16_t j;
            for (j = 1; j < nlen; j++) {
                if (hay[i + j] != needle[j]) break;
            }
            if (j == nlen) return hay + i;
        }
    }
    return NULL;
}

/* Print one line from `p` up to (but not including) the next \r or \n. */
static const char *print_line(const char *p, const char *end)
{
    const char *q = p;
    while (q < end && *q != '\r' && *q != '\n') uart_putc(*q++);
    uart_puts("\r\n");
    /* skip \r\n */
    if (q < end && *q == '\r') q++;
    if (q < end && *q == '\n') q++;
    return q;
}

/* =========================================================================
 * http_task
 * ========================================================================= */

WORD http_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam;
    (void)lParam;

    if (MsgType == KTOS_MSG_TYPE_INIT) return MSG_WAIT;
    if (MsgType != MSG_HTTP_DONE)      return MSG_WAIT;

    delay_ms(50);
    uart_puts("\033[2J\033[H");

    uart_puts("=============================\r\n");
#if HTTP_USE_SSL
    uart_puts("   KTOS HTTPS GET\r\n");
#else
    uart_puts("   KTOS HTTP GET\r\n");
#endif
    uart_puts("=============================\r\n");

    uart_puts("Host   : " HTTP_HOST "\r\n");
    uart_puts("Path   : " HTTP_PATH "\r\n");
#if HTTP_USE_SSL
    uart_puts("Mode   : HTTPS (axTLS"
#if HTTP_VERIFY_CA
              ", CA verify"
#endif
#if HTTP_CLIENT_CERT
              ", client cert"
#endif
              ")\r\n");
#else
    uart_puts("Mode   : HTTP\r\n");
#endif
    uart_puts("----\r\n");

    /* Error cases */
    if (g_resp_err == 1) {
        uart_puts("Error  : DNS resolution failed for " HTTP_HOST "\r\n");
        return MSG_WAIT;
    }
    if (g_resp_err == 2) {
#if HTTP_USE_SSL
        uart_puts("Error  : TLS/TCP connection failed.\r\n");
        uart_puts("         Server may require TLS 1.2+ or ECDHE (not supported\r\n");
        uart_puts("         by axTLS).  Try HTTP_USE_SSL 0 or a different server.\r\n");
#else
        uart_puts("Error  : TCP connection failed.\r\n");
#endif
        return MSG_WAIT;
    }
    if (g_resp_err == 3) {
        uart_puts("Error  : SNTP time sync timed out.\r\n");
        uart_puts("         CA verification requires a valid clock.\r\n");
        uart_puts("         Check that UDP port 123 is reachable or increase\r\n");
        uart_puts("         HTTP_SNTP_TIMEOUT_MS in http_config.h.\r\n");
        return MSG_WAIT;
    }
    if (g_resp_len == 0) {
        uart_puts("Error  : Empty response.\r\n");
        return MSG_WAIT;
    }

    const char *buf = g_resp_buf;
    const char *end = buf + g_resp_len;

    /* Print HTTP status line (first line of response). */
    uart_puts("Status : ");
    buf = print_line(buf, end);

    /* Find header / body separator (\r\n\r\n). */
    const char *body = mem_find(g_resp_buf, g_resp_len, "\r\n\r\n", 4);
    if (body) {
        body += 4; /* skip the separator */
        uint16_t body_len = (uint16_t)(end - body);
        uart_puts("Body   :\r\n");
        uart_write(body, body_len);
        if (body_len > 0 && body[body_len - 1] != '\n')
            uart_puts("\r\n");
    } else {
        /* No separator — print everything as body. */
        uart_puts("Body   :\r\n");
        uart_write(g_resp_buf, g_resp_len);
        uart_puts("\r\n");
    }

    uart_puts("----\r\n");
    uart_puts("Done. (");
    uart_putu(g_resp_len);
    uart_puts(" bytes total)\r\n");

    return MSG_WAIT;
}

/* =========================================================================
 * CALL0 application entry point — called from startup.c via callx0
 * ========================================================================= */

void ktos_app_main(void)
{
    struct ktos_TASK *http = ktos_InitTask(http_task, TASK_MAIN_STACK_SIZE,
                                            TASK_MAIN_QUEUE_SIZE, 'H');
    ktos_SendMsg(http, MSG_HTTP_DONE, 0, 0);

    ktos_RunOS(); /* never returns */
}
