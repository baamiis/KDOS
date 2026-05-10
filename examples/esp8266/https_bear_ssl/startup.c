/**
 * @file startup.c
 * @brief Windowed-ABI entry point for the KTOS BearSSL TLS 1.2 GET demo.
 *
 * Flow:
 *  1. user_pre_init()    — register partition table.
 *  2. user_init()        — STATION_MODE, credentials, event handler.
 *  3. on_sdk_ready()     — explicit wifi_station_connect().
 *  4. on_wifi_event()    — on GOT_IP: resolve HTTP_HOST via DNS.
 *  5. dns_done_cb()      — plain espconn TCP connect (BearSSL owns TLS).
 *  6. tcp_connect_cb()   — bearssl_init(): kicks the TLS handshake.
 *  7. bearssl_on_recv()  — feeds TCP bytes into the BearSSL state machine.
 *  8. bearssl_on_sent()  — releases BearSSL's send buffer, drives engine.
 *  9. BearSSL engine     — on SENDAPP: injects HTTP GET; on RECVAPP: buffers response.
 * 10. BR_SSL_CLOSED      — bearssl_conn.c calls do_exit() → callx0 to KTOS.
 */

#include "user_interface.h"
#include "espconn.h"
#include "osapi.h"
#include <stddef.h>
#include <string.h>
#include "wifi_config.h"
#include "http_config.h"
#include "bearssl_conn.h"

extern void ets_wdt_disable(void);

/* FRC1 — stopped before abandoning ets_run() */
#define FRC1_CTRL  (*(volatile uint32_t *)0x60000608UL)
#define FRC1_INT   (*(volatile uint32_t *)0x6000060CUL)

/* =========================================================================
 * Partition table — 4 MB flash, map 4
 * ========================================================================= */
#define PART_OTA_SIZE    0x06A000UL
#define PART_OTA2_ADDR   0x081000UL
#define PART_RF_CAL_ADDR 0x3FB000UL
#define PART_PHY_ADDR    0x3FC000UL
#define PART_SYS_ADDR    0x3FD000UL

static const partition_item_t ktos_partition_table[] = {
    { SYSTEM_PARTITION_BOOTLOADER,             0x000000,        0x001000 },
    { SYSTEM_PARTITION_OTA_1,                  0x001000,        PART_OTA_SIZE },
    { SYSTEM_PARTITION_OTA_2,                  PART_OTA2_ADDR,  PART_OTA_SIZE },
    { SYSTEM_PARTITION_RF_CAL,                 PART_RF_CAL_ADDR, 0x001000 },
    { SYSTEM_PARTITION_PHY_DATA,               PART_PHY_ADDR,    0x001000 },
    { SYSTEM_PARTITION_SYSTEM_PARAMETER,       PART_SYS_ADDR,    0x003000 },
};

/* =========================================================================
 * Shared response buffer — written here and by bearssl_conn.c, read by main.c
 * ========================================================================= */

char     g_resp_buf[HTTP_RESP_BUF];
uint16_t g_resp_len;
uint8_t  g_resp_err;   /* 0=ok, 1=DNS fail, 2=TLS/TCP fail */
int      g_ssl_err;    /* BearSSL last_error() value when g_resp_err==2 */

/* =========================================================================
 * espconn state
 * ========================================================================= */

static struct espconn g_conn;
static esp_tcp        g_tcp;
static ip_addr_t      g_server_ip;

extern void ktos_app_main(void);

/* =========================================================================
 * ABI crossing — called from bearssl_conn.c once the response is complete
 * ========================================================================= */

void do_exit(void)
{
    FRC1_CTRL = 0;
    FRC1_INT  = 0;
    /* WDTs already disabled in tcp_connect_cb(); safe to call again. */
    ets_wdt_disable();
    system_soft_wdt_stop();
    __asm__ volatile ("callx0 %0" :: "r"(ktos_app_main) : "a0", "memory");
    while (1);
}

/* =========================================================================
 * espconn callbacks
 * ========================================================================= */

static void ICACHE_FLASH_ATTR tcp_connect_cb(void *arg)
{
    struct espconn *conn = (struct espconn *)arg;

    os_printf("[KTOS] TCP connected — disabling WDT, starting TLS\n");

    /* BearSSL RSA verification can block for several seconds on 80 MHz.
     * Disable both watchdogs now; do_exit() is the only way out from here. */
    ets_wdt_disable();
    system_soft_wdt_stop();

    /* Register BearSSL-driven callbacks — BearSSL owns TLS from here. */
    espconn_regist_recvcb  (conn, bearssl_on_recv);
    espconn_regist_sentcb  (conn, bearssl_on_sent);
    espconn_regist_reconcb (conn, bearssl_on_error);
    espconn_regist_disconcb(conn, bearssl_on_discon);

    /* Start TLS handshake — first call drives ClientHello into SENDREC. */
    bearssl_init(conn, HTTP_HOST);
}

static void ICACHE_FLASH_ATTR tcp_error_cb(void *arg, sint8 err)
{
    (void)arg;
    os_printf("[KTOS] TCP error %d\n", (int)err);
    g_resp_err = 2;
    do_exit();
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
        os_printf("[KTOS] DNS failed for " HTTP_HOST "\n");
        g_resp_err = 1;
        do_exit();
        return;
    }
    os_printf("[KTOS] DNS OK -> %d.%d.%d.%d, connecting...\n",
              (int)((ipaddr->addr >>  0) & 0xFF),
              (int)((ipaddr->addr >>  8) & 0xFF),
              (int)((ipaddr->addr >> 16) & 0xFF),
              (int)((ipaddr->addr >> 24) & 0xFF));

    g_server_ip = *ipaddr;

    g_tcp.remote_port = HTTP_PORT;
    g_tcp.remote_ip[0] = (uint8_t)((ipaddr->addr >>  0) & 0xFFu);
    g_tcp.remote_ip[1] = (uint8_t)((ipaddr->addr >>  8) & 0xFFu);
    g_tcp.remote_ip[2] = (uint8_t)((ipaddr->addr >> 16) & 0xFFu);
    g_tcp.remote_ip[3] = (uint8_t)((ipaddr->addr >> 24) & 0xFFu);

    conn->type      = ESPCONN_TCP;
    conn->state     = ESPCONN_NONE;
    conn->proto.tcp = &g_tcp;

    /* Only the connect callback is registered here; the rest are wired inside
     * tcp_connect_cb() after BearSSL takes ownership of the connection. */
    espconn_regist_connectcb(conn, tcp_connect_cb);
    espconn_regist_reconcb  (conn, tcp_error_cb);

    espconn_connect(conn); /* plain TCP — BearSSL handles TLS above it */
}

/* =========================================================================
 * WiFi event handler
 * ========================================================================= */

static void ICACHE_FLASH_ATTR on_wifi_event(System_Event_t *evt)
{
    if (evt->event != EVENT_STAMODE_GOT_IP)
        return;

    os_printf("[KTOS] WiFi GOT_IP — resolving " HTTP_HOST "\n");
    espconn_gethostbyname(&g_conn, HTTP_HOST, &g_server_ip, dns_done_cb);
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
    /* Match boot ROM baud so miniterm at 74880 shows our debug output too. */
    uart_div_modify(0, 80000000 / 74880);
    system_set_os_print(1);
    os_printf("\n[KTOS] user_init\n");

    wifi_set_opmode(STATION_MODE);

    struct station_config cfg;
    memset(&cfg, 0, sizeof cfg);
    memcpy(cfg.ssid,     WIFI_SSID,     sizeof(WIFI_SSID)     - 1);
    memcpy(cfg.password, WIFI_PASSWORD, sizeof(WIFI_PASSWORD) - 1);
    wifi_station_set_config(&cfg);
    wifi_station_set_auto_connect(0);

    wifi_set_event_handler_cb(on_wifi_event);
    system_init_done_cb(on_sdk_ready);
}
