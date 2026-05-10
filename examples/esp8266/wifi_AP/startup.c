/**
 * @file startup.c
 * @brief Windowed-ABI entry point for the KTOS WiFi AP demo.
 *
 * Compiled WITHOUT -mabi=call0 (windowed ABI) so the SDK's call_user_start
 * can invoke user_pre_init() and user_init() normally via CALL4.
 *
 * Flow:
 *  1. user_pre_init() — register flash partition table (required by this SDK).
 *  2. user_init()     — set SOFTAP_MODE, configure AP (from wifi_ap_config.h),
 *                       register system_init_done_cb.
 *  3. on_sdk_ready()  — called by SDK after RF init; read AP IP, then cross
 *                       into CALL0 ktos_app_main via callx0.  Never returns.
 *
 * Flash size: 4 MB (32 Mbit) NodeMCU / Wemos D1 Mini.
 * Pass -DSPI_FLASH_SIZE_MAP=4 in CFLAGS for this board.
 */

#include "user_interface.h"
#include <stddef.h>
#include <string.h>
#include "wifi_ap_config.h"

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

/* AP IP — set by on_sdk_ready() after RF init; read by main.c via extern. */
uint32_t g_ap_ip;

extern void ktos_app_main(void);

/* =========================================================================
 * SDK callbacks — windowed ABI
 * ========================================================================= */

static void ICACHE_FLASH_ATTR on_sdk_ready(void)
{
    struct ip_info info;
    wifi_get_ip_info(SOFTAP_IF, &info);
    g_ap_ip = info.ip.addr;

    /* Disable watchdogs for the duration of KTOS execution.  Both functions
     * are windowed-ABI and must be called here (not from ktos_app_main).
     * FRC1 is intentionally left running so the SDK's os_timer callbacks
     * (beacon management, DHCP server) continue while KTOS is active. */
    ets_wdt_disable();
    system_soft_wdt_stop();

    /* Plain function call — both compilation units use windowed ABI.
     * ktos_app_main() calls ktos_ExitOS() via ap_task and returns here
     * once the print task is done.  Returning to ets_run() keeps the AP
     * beaconing and the DHCP server alive. */
    ktos_app_main();
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
    wifi_set_opmode(SOFTAP_MODE);

    struct softap_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.ssid,     AP_SSID,     sizeof(AP_SSID) - 1);
    cfg.ssid_len = (uint8_t)(sizeof(AP_SSID) - 1);
    memcpy(cfg.password, AP_PASSWORD, sizeof(AP_PASSWORD) - 1);
    cfg.channel         = AP_CHANNEL;
    cfg.authmode        = AUTH_WPA2_PSK;
    cfg.max_connection  = 4;
    cfg.beacon_interval = 100;
    wifi_softap_set_config(&cfg);

    system_init_done_cb(on_sdk_ready);
}
