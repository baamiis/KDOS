/**
 * @file main.c
 * @brief KTOS WiFi scan demo for ESP8266 (Xtensa LX106).
 *
 * Execution flow:
 *   1. startup.c (windowed ABI) handles SDK init: partition table,
 *      STATION_MODE, RF calibration, and the channel scan.
 *      The ESP8266 Non-OS SDK's ets_run() loop — a blocking ROM function
 *      with no single-iteration API — must own the CPU while the WiFi stack
 *      processes scan results.  KTOS cannot coexist with it concurrently.
 *   2. When the scan completes, on_scan_done() crosses into CALL0 via
 *      callx0 → ktos_app_main().  ets_run() is abandoned; it never resumes.
 *   3. ktos_app_main() creates wifi_task and posts MSG_SCAN_DONE with the
 *      scan result pointer, then starts the KTOS scheduler.
 *   4. wifi_task receives MSG_SCAN_DONE, prints the BSS list, sleeps forever.
 *
 * Expected output (74880 8N1):
 * @code
 * =============================
 *   KTOS WiFi Scan
 * =============================
 *  #   SSID                             CH   RSSI  AUTH
 * ---  -------------------------------- ---  ----  ------
 *   1  HomeNetwork                        6   -67  WPA2
 *   2  GuestNet                           1   -72  OPEN
 * ----
 * Scan complete — 2 networks found.
 * @endcode
 */

#include "../../../core/ktos.h"
#include <stdint.h>

/* Application-defined message type: scan complete, lParam = bss_info pointer. */
#define MSG_SCAN_DONE  ((WORD)(KTOS_MSG_TYPE_SYSTEM_START))

/* =========================================================================
 * bss_info layout — mirrors ESP8266 Non-OS SDK struct bss_info exactly.
 *
 * We do NOT include user_interface.h here because it defines "bool" as
 * "unsigned char", conflicting with <stdbool.h> (included via ktos.h).
 * This struct is byte-compatible with the SDK version; AUTH_MODE values
 * are reproduced as plain defines below.
 *
 * Field offsets (Xtensa LX106, no packing):
 *   0   next.stqe_next   4 B  (STAILQ pointer)
 *   4   bssid[6]         6 B
 *  10   ssid[32]        32 B
 *  42   ssid_len         1 B
 *  43   channel          1 B
 *  44   rssi             1 B  (signed)
 *  45   _pad[3]          3 B  (align AUTH_MODE to 4-byte boundary)
 *  48   authmode         4 B  (AUTH_MODE enum → int-sized)
 * ========================================================================= */

#define AUTH_OPEN         0
#define AUTH_WEP          1
#define AUTH_WPA_PSK      2
#define AUTH_WPA2_PSK     3
#define AUTH_WPA_WPA2_PSK 4

struct bss_info {
    struct { struct bss_info *stqe_next; } next; /* STAILQ_ENTRY */
    uint8_t  bssid[6];
    uint8_t  ssid[32];
    uint8_t  ssid_len;
    uint8_t  channel;
    int8_t   rssi;
    uint8_t  _pad[3];    /* 3 bytes padding — align authmode to 4 bytes */
    uint32_t authmode;   /* AUTH_MODE enum, int-sized */
};

/* Set by startup.c on_scan_done() before calling ktos_app_main(). */
extern void *g_scan_results;

/* =========================================================================
 * UART0 — direct register access, 74880 8N1
 *
 * The SDK configures UART0 for 74880 baud 8N1 before calling user_init().
 * We inherit that configuration — touching CLKDIV or CONF0 while the FIFO
 * may still hold SDK bytes causes framing errors and garbage output.
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

/* Print string left-aligned, padded with spaces to `width`. */
static void uart_puts_ljust(const char *s, int width)
{
    int n = 0;
    while (s[n]) { uart_putc(s[n++]); }
    while (n < width) { uart_putc(' '); n++; }
}

/* Print unsigned decimal right-aligned in a field of `width` chars. */
static void uart_putu_rj(uint32_t n, int width)
{
    char buf[10];
    int i = 0;
    if (n == 0) { buf[i++] = '0'; }
    else { uint32_t t = n; while (t) { buf[i++] = '0' + (char)(t % 10); t /= 10; } }
    for (int j = i; j < width; j++) uart_putc(' ');
    while (i--) uart_putc(buf[i]);
}

/* =========================================================================
 * Delay — Xtensa CCOUNT at ~52 MHz
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
 * Auth mode label
 * ========================================================================= */

static const char *auth_str(uint32_t m)
{
    switch (m) {
        case AUTH_OPEN:         return "OPEN";
        case AUTH_WEP:          return "WEP";
        case AUTH_WPA_PSK:      return "WPA";
        case AUTH_WPA2_PSK:     return "WPA2";
        case AUTH_WPA_WPA2_PSK: return "WPA/2";
        default:                return "?";
    }
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
 * wifi_task — INIT sleeps waiting for MSG_SCAN_DONE; MSG_SCAN_DONE prints BSS list
 * ========================================================================= */

WORD wifi_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam;

    /* INIT: task is live; wait for the scan-result message from ktos_app_main. */
    if (MsgType == KTOS_MSG_TYPE_INIT) return MSG_WAIT;

    if (MsgType != MSG_SCAN_DONE) return MSG_WAIT;

    delay_ms(50);
    uart_puts("\033[2J\033[H"); /* clear ROM boot noise from terminal */

    uart_puts("=============================\r\n");
    uart_puts("   KTOS WiFi Scan\r\n");
    uart_puts("=============================\r\n");

    struct bss_info *bss = (struct bss_info *)(uintptr_t)lParam;
    if (!bss) {
        uart_puts("Scan failed or no networks found.\r\n");
        return MSG_WAIT;
    }

    uart_puts(" #   SSID                             CH   RSSI  AUTH\r\n");
    uart_puts("---  -------------------------------- ---  ----  ------\r\n");

    int count = 0;
    while (bss) {
        ++count;
        uart_putu_rj((uint32_t)count, 3);
        uart_puts("  ");

        /* SSID may not be NUL-terminated when ssid_len == 32. */
        char ssid_buf[33];
        uint8_t slen = bss->ssid_len < 32 ? bss->ssid_len : 32;
        for (uint8_t i = 0; i < slen; i++) ssid_buf[i] = (char)bss->ssid[i];
        ssid_buf[slen] = '\0';
        uart_puts_ljust(ssid_buf, 32);

        uart_putc(' ');
        uart_putu_rj(bss->channel, 3);
        uart_puts("  ");

        /* RSSI is negative; right-pad the column to 4 chars. */
        {
            int32_t r = (int32_t)bss->rssi;
            uart_puti(r);
            int w = (r < 0) ? 1 : 0;
            uint32_t av = (r < 0) ? (uint32_t)(-r) : (uint32_t)r;
            if (av == 0) w++;
            else { uint32_t t = av; while (t) { t /= 10; w++; } }
            for (; w < 4; w++) uart_putc(' ');
        }
        uart_puts("  ");
        uart_puts(auth_str(bss->authmode));
        uart_puts("\r\n");

        bss = bss->next.stqe_next;
    }

    uart_puts("----\r\n");
    uart_puts("Scan complete - ");
    uart_putu((uint32_t)count);
    uart_puts(count == 1 ? " network found.\r\n" : " networks found.\r\n");

    return MSG_WAIT;
}

/* =========================================================================
 * CALL0 application entry point — called from startup.c via callx0
 * ========================================================================= */

void ktos_app_main(void)
{
    struct ktos_TASK *wifi = ktos_InitTask(wifi_task, TASK_MAIN_STACK_SIZE,
                                            TASK_MAIN_QUEUE_SIZE, 'W');
    /* Deliver scan results as a KTOS message before the scheduler starts.
     * wifi_task will receive INIT first (stack pre-loaded), then MSG_SCAN_DONE. */
    ktos_SendMsg(wifi, MSG_SCAN_DONE, 0, (LONG)(uintptr_t)g_scan_results);

    ktos_RunOS(); /* never returns */
}
