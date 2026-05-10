/**
 * @file startup.c
 * @brief Windowed-ABI entry point for the KTOS WiFi connect demo.
 *
 * Compiled WITHOUT -mabi=call0 (windowed ABI) so the SDK's call_user_start
 * can invoke user_pre_init() and user_init() normally via CALL4.
 *
 * Flow:
 *  1. user_pre_init() — register flash partition table (required by this SDK).
 *  2. user_init()     — set STATION_MODE, configure credentials (from
 *                       wifi_config.h), register event handler,
 *                       register system_init_done_cb.
 *  3. on_sdk_ready()  — called after RF init; kick off the connection attempt.
 *  4. on_wifi_event() — called by SDK on station state changes.
 *                       On EVENT_STAMODE_GOT_IP: save IP/mask/gw/RSSI,
 *                       then cross into CALL0 ktos_app_main via callx0.
 *                       Never returns.
 *
 * Flash size: 4 MB (32 Mbit) NodeMCU / Wemos D1 Mini.
 * Pass -DSPI_FLASH_SIZE_MAP=4 in CFLAGS for this board.
 */

#include "user_interface.h"
#include <stddef.h>
#include <string.h>
#include "wifi_config.h"

extern void ets_wdt_disable(void);

/* =========================================================================
 * Partition table — 4 MB (32 Mbit) flash, map 4 (512 KB + 512 KB OTA)
 * ========================================================================= */
#define PART_OTA_SIZE      0x06A000UL
#define PART_OTA2_ADDR     0x081000UL
#define PART_RF_CAL_ADDR   0x3FB000UL
#define PART_PHY_ADDR      0x3FC000UL
#define PART_SYS_ADDR      0x3FD000UL

static const partition_item_t ktos_partition_table[] = {
    { SYSTEM_PARTITION_BOOTLOADER,        0x000000, 0x001000 },
    { SYSTEM_PARTITION_OTA_1,             0x001000, PART_OTA_SIZE },
    { SYSTEM_PARTITION_OTA_2,             PART_OTA2_ADDR, PART_OTA_SIZE },
    { SYSTEM_PARTITION_RF_CAL,            PART_RF_CAL_ADDR, 0x001000 },
    { SYSTEM_PARTITION_PHY_DATA,          PART_PHY_ADDR,    0x001000 },
    { SYSTEM_PARTITION_SYSTEM_PARAMETER,  PART_SYS_ADDR,    0x003000 },
};

/* Connection info — set by on_wifi_event() on GOT_IP; read by main.c. */
uint32_t g_conn_ip;
uint32_t g_conn_gw;
uint32_t g_conn_mask;
int8_t   g_conn_rssi;

extern void ktos_app_main(void);

/* =========================================================================
 * WiFi event handler — windowed ABI, called by SDK's ets_run() loop
 * ========================================================================= */

/* FRC1 control and interrupt-clear registers. */
#define FRC1_CTRL  (*(volatile uint32_t *)0x60000608UL)
#define FRC1_INT   (*(volatile uint32_t *)0x6000060CUL)

static void ICACHE_FLASH_ATTR on_wifi_event(System_Event_t *evt)
{
    if (evt->event != EVENT_STAMODE_GOT_IP)
        return;

    g_conn_ip   = evt->event_info.got_ip.ip.addr;
    g_conn_gw   = evt->event_info.got_ip.gw.addr;
    g_conn_mask = evt->event_info.got_ip.mask.addr;
    /* wifi_station_get_rssi() returns the signal strength in dBm (negative),
     * or 31 if unavailable.  Cast is safe: valid RSSI fits in int8_t. */
    g_conn_rssi = (int8_t)wifi_station_get_rssi();

    /* Stop FRC1 before abandoning ets_run().  The SDK's FRC1 ISR drives
     * os_timer callbacks that may write debug output to UART once
     * ets_run() is no longer processing their posted events. */
    FRC1_CTRL = 0;
    FRC1_INT  = 0;

    /* Stop both watchdog timers before crossing into ktos_app_main. */
    ets_wdt_disable();
    system_soft_wdt_stop();

    __asm__ volatile ("callx0 %0" :: "r"(ktos_app_main) : "a0", "memory");
    while (1);
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
    wifi_set_opmode(STATION_MODE);

    struct station_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.ssid,     WIFI_SSID,     sizeof(WIFI_SSID) - 1);
    memcpy(cfg.password, WIFI_PASSWORD, sizeof(WIFI_PASSWORD) - 1);
    wifi_station_set_config(&cfg);

    /* Disable auto-connect so we can drive the connection explicitly from
     * on_sdk_ready() once RF is calibrated and ready. */
    wifi_station_set_auto_connect(0);

    wifi_set_event_handler_cb(on_wifi_event);

    system_init_done_cb(on_sdk_ready);
}
