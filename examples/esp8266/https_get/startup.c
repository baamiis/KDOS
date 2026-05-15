/**
 * @file startup.c
 * @brief Windowed-ABI entry point for the KTOS HTTP(S) GET demo.
 *
 * All network operations run inside ets_run() via espconn callbacks.
 * Once the full HTTP response has been received (disconnect callback),
 * we stop FRC1, disable both WDTs, and cross into CALL0 ktos_app_main
 * via callx0.  ets_run() is then abandoned — no further WiFi needed.
 *
 * Flow:
 *  1. user_pre_init()   — register partition table.
 *  2. user_init()       — STATION_MODE, credentials, event handler.
 *  3. on_sdk_ready()    — explicit wifi_station_connect().
 *  4. on_wifi_event()   — on GOT_IP: resolve HTTP_HOST via DNS.
 *  5. dns_done_cb()     — set up espconn, (secure_)connect to server.
 *  6. http_connect_cb() — send HTTP GET request.
 *  7. http_recv_cb()    — accumulate response bytes into g_resp_buf.
 *  8. http_discon_cb()  — response complete: stop FRC1, callx0 to KTOS.
 *     http_error_cb()   — connection/SSL error: same exit path.
 */

#include "user_interface.h"
#include "espconn.h"
#include "osapi.h"
#include "sntp.h"
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "wifi_config.h"
#include "http_config.h"

extern void ets_wdt_disable(void);

/* FRC1 — stopped before abandoning ets_run() to prevent the SDK's os_timer
 * ISR from corrupting UART output once event processing ceases. */
#define FRC1_CTRL  (*(volatile uint32_t *)0x60000608UL)
#define FRC1_INT   (*(volatile uint32_t *)0x6000060CUL)

/* =========================================================================
 * Partition table — 4 MB (32 Mbit) flash, map 4
 * ========================================================================= */
#define PART_OTA_SIZE      0x06A000UL
#define PART_OTA2_ADDR     0x081000UL
#define PART_RF_CAL_ADDR   0x3FB000UL
#define PART_PHY_ADDR      0x3FC000UL
#define PART_SYS_ADDR      0x3FD000UL

static const partition_item_t ktos_partition_table[] = {
    { SYSTEM_PARTITION_BOOTLOADER,              0x000000,       0x001000 },
    { SYSTEM_PARTITION_OTA_1,                   0x001000,       PART_OTA_SIZE },
    { SYSTEM_PARTITION_OTA_2,                   PART_OTA2_ADDR, PART_OTA_SIZE },
    /* Cert sectors live in the free gap between OTA2 end (0x0EB000) and RF_CAL */
    { SYSTEM_PARTITION_SSL_CLIENT_CA,           0x0EC000,       0x001000 },
    { SYSTEM_PARTITION_SSL_CLIENT_CERT_PRIVKEY, 0x0ED000,       0x001000 },
    { SYSTEM_PARTITION_RF_CAL,                  PART_RF_CAL_ADDR, 0x001000 },
    { SYSTEM_PARTITION_PHY_DATA,                PART_PHY_ADDR,    0x001000 },
    { SYSTEM_PARTITION_SYSTEM_PARAMETER,        PART_SYS_ADDR,    0x003000 },
};

/* =========================================================================
 * Shared response buffer — written here, read by main.c via extern.
 * ========================================================================= */

char     g_resp_buf[HTTP_RESP_BUF]; /* raw HTTP response (headers + body)  */
uint16_t g_resp_len;                /* bytes written into g_resp_buf        */
uint8_t  g_resp_err;                /* 0 = success, 1 = DNS fail,
                                       2 = connect/SSL fail, 3 = timeout   */

/* =========================================================================
 * espconn state
 * ========================================================================= */

static struct espconn g_conn;
static esp_tcp        g_tcp;
static ip_addr_t      g_server_ip;

/* Pre-built HTTP request — compile-time string concatenation. */
static const char g_request[] =
    "GET " HTTP_PATH " HTTP/1.1\r\n"
    "Host: " HTTP_HOST "\r\n"
    "Connection: close\r\n"
    "User-Agent: KTOS/ESP8266\r\n"
    "\r\n";

extern void ktos_app_main(void);

/* =========================================================================
 * ABI crossing — called once, never returns
 * ========================================================================= */

static void do_exit(void)
{
    FRC1_CTRL = 0;
    FRC1_INT  = 0;
    ets_wdt_disable();
    system_soft_wdt_stop();
    __asm__ volatile ("callx0 %0" :: "r"(ktos_app_main) : "a0", "memory");
    while (1);
}

/* =========================================================================
 * espconn callbacks — windowed ABI, called from ets_run()
 * ========================================================================= */

static void ICACHE_FLASH_ATTR http_recv_cb(void *arg, char *data, uint16_t len)
{
    (void)arg;
    uint16_t space = (uint16_t)(HTTP_RESP_BUF - 1) - g_resp_len;
    if (len > space) len = space;
    if (len > 0) {
        memcpy(g_resp_buf + g_resp_len, data, len);
        g_resp_len += len;
    }
}

static void ICACHE_FLASH_ATTR http_discon_cb(void *arg)
{
    (void)arg;
    g_resp_buf[g_resp_len] = '\0';
    do_exit();
}

static void ICACHE_FLASH_ATTR http_error_cb(void *arg, sint8 err)
{
    (void)arg; (void)err;
    g_resp_err = 2;
    do_exit();
}

static void ICACHE_FLASH_ATTR http_connect_cb(void *arg)
{
    struct espconn *conn = (struct espconn *)arg;
#if HTTP_USE_SSL
    espconn_secure_sent(conn, (uint8_t *)g_request,
                        (uint16_t)(sizeof(g_request) - 1));
#else
    espconn_sent(conn, (uint8_t *)g_request,
                 (uint16_t)(sizeof(g_request) - 1));
#endif
}

/* =========================================================================
 * DNS callback
 * ========================================================================= */

static void ICACHE_FLASH_ATTR dns_done_cb(const char *name,
                                           ip_addr_t  *ipaddr,
                                           void       *arg)
{
    (void)name;
    struct espconn *conn = (struct espconn *)arg;

    if (!ipaddr) {
        g_resp_err = 1; /* DNS failed */
        do_exit();
        return;
    }

    g_server_ip = *ipaddr;

    g_tcp.remote_port = HTTP_PORT;
    g_tcp.remote_ip[0] = (uint8_t)((ipaddr->addr >>  0) & 0xFFu);
    g_tcp.remote_ip[1] = (uint8_t)((ipaddr->addr >>  8) & 0xFFu);
    g_tcp.remote_ip[2] = (uint8_t)((ipaddr->addr >> 16) & 0xFFu);
    g_tcp.remote_ip[3] = (uint8_t)((ipaddr->addr >> 24) & 0xFFu);

    conn->type       = ESPCONN_TCP;
    conn->state      = ESPCONN_NONE;
    conn->proto.tcp  = &g_tcp;

    espconn_regist_connectcb(conn, http_connect_cb);
    espconn_regist_recvcb   (conn, http_recv_cb);
    espconn_regist_disconcb (conn, http_discon_cb);
    espconn_regist_reconcb  (conn, http_error_cb);

#if HTTP_USE_SSL
    /* Limit SSL buffer to HTTP_SSL_BUF bytes.  KTOS frees ~36 KB vs Arduino,
     * giving axTLS enough heap for the TLS handshake on most servers that
     * still accept TLS 1.0/1.1 with RSA key exchange. */
    espconn_secure_set_size(ESPCONN_CLIENT, HTTP_SSL_BUF);
#if HTTP_VERIFY_CA
    espconn_secure_ca_enable(ESPCONN_CLIENT, HTTP_CA_FLASH_SECTOR);
#endif
#if HTTP_CLIENT_CERT
    espconn_secure_cert_req_enable(ESPCONN_CLIENT, HTTP_CLIENT_FLASH_SECTOR);
#endif
    espconn_secure_connect(conn);
#else
    espconn_connect(conn);
#endif
}

/* =========================================================================
 * SNTP — sync wall-clock time before TLS handshake (CA verify only)
 *
 * axTLS checks the server cert's notBefore/notAfter fields.  Without a
 * valid clock every cert looks expired.  We poll sntp_get_current_timestamp()
 * in a repeating os_timer; once a non-zero value comes back we stop the
 * timer and proceed to DNS.  If no response arrives within
 * HTTP_SNTP_TIMEOUT_MS we abort with g_resp_err = 3.
 * ========================================================================= */

#if HTTP_VERIFY_CA && HTTP_SNTP_SYNC

#define SNTP_POLL_MS  200u
#define SNTP_MAX_POLLS ((HTTP_SNTP_TIMEOUT_MS) / (SNTP_POLL_MS))

static os_timer_t g_sntp_timer;
static uint16_t   g_sntp_polls;

static void ICACHE_FLASH_ATTR sntp_check_cb(void *arg)
{
    (void)arg;

    if (sntp_get_current_timestamp() == 0) {
        if (++g_sntp_polls < SNTP_MAX_POLLS) return; /* still waiting */
        os_timer_disarm(&g_sntp_timer);
        sntp_stop();
        g_resp_err = 3; /* timeout */
        do_exit();
        return;
    }

    os_timer_disarm(&g_sntp_timer);
    sntp_stop();
    espconn_gethostbyname(&g_conn, HTTP_HOST, &g_server_ip, dns_done_cb);
}

#endif /* HTTP_VERIFY_CA && HTTP_SNTP_SYNC */

/* =========================================================================
 * WiFi event handler
 * ========================================================================= */

static void ICACHE_FLASH_ATTR on_wifi_event(System_Event_t *evt)
{
    if (evt->event != EVENT_STAMODE_GOT_IP)
        return;

#if HTTP_VERIFY_CA && HTTP_SNTP_SYNC
    /* Sync the clock before the TLS handshake — cert time validation needs it. */
    sntp_setservername(0, HTTP_SNTP_SERVER);
    sntp_init();
    g_sntp_polls = 0;
    os_timer_disarm(&g_sntp_timer);
    os_timer_setfn(&g_sntp_timer, sntp_check_cb, NULL);
    os_timer_arm(&g_sntp_timer, SNTP_POLL_MS, 1 /* repeat */);
#else
    espconn_gethostbyname(&g_conn, HTTP_HOST, &g_server_ip, dns_done_cb);
#endif
}

static void ICACHE_FLASH_ATTR on_sdk_ready(void)
{
    wifi_station_connect();
}

/* =========================================================================
 * SDK entry points
 * ========================================================================= */

void ICACHE_FLASH_ATTR user_pre_init(void)
{
    system_partition_table_regist(
        ktos_partition_table,
        sizeof(ktos_partition_table) / sizeof(ktos_partition_table[0]),
        SPI_FLASH_SIZE_MAP);
}

void ICACHE_FLASH_ATTR user_init(void)
{
    /* Suppress SDK/axTLS debug prints (e.g. "SSL error 3" on close_notify). */
    system_set_os_print(0);

    wifi_set_opmode(STATION_MODE);

    struct station_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.ssid,     WIFI_SSID,     sizeof(WIFI_SSID)     - 1);
    memcpy(cfg.password, WIFI_PASSWORD, sizeof(WIFI_PASSWORD) - 1);
    wifi_station_set_config(&cfg);
    wifi_station_set_auto_connect(0);

    wifi_set_event_handler_cb(on_wifi_event);
    system_init_done_cb(on_sdk_ready);
}
