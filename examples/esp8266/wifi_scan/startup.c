/**
 * @file startup.c
 * @brief Windowed-ABI entry point for the KTOS WiFi scan demo.
 *
 * Compiled WITHOUT -mabi=call0 (windowed ABI) so the SDK's call_user_start
 * can invoke user_pre_init() and user_init() normally via CALL4.
 *
 * Flow:
 *  1. user_pre_init() — register flash partition table (required by this SDK).
 *  2. user_init()     — set STATION_MODE, register system_init_done_cb.
 *  3. on_sdk_ready()  — called by SDK after RF init; start channel scan.
 *  4. on_scan_done()  — called by SDK when scan completes; save BSS list,
 *                       then cross into CALL0 ktos_app_main via callx0.
 *                       Never returns.
 *
 * Flash size: 4 MB (32 Mbit) NodeMCU / Wemos D1 Mini.
 * Pass -DSPI_FLASH_SIZE_MAP=4 in CFLAGS for this board.
 */

#include "user_interface.h"   /* wifi_*, system_*, partition_item_t, ICACHE_FLASH_ATTR */
#include <stddef.h>           /* NULL */

/* ROM hardware WDT — stops the independent ets_wdt timer (separate from the
 * SDK software WDT).  Not in user_interface.h; linked via eagle.rom.addr.v6.ld. */
extern void ets_wdt_disable(void);

/* =========================================================================
 * Partition table — 4 MB (32 Mbit) flash, map 4 (512 KB + 512 KB OTA)
 *
 * esptool write_flash --flash_size 4MB writes header byte 0x40; the SDK
 * decodes this as spi_size_map=4 (FLASH_SIZE_32M_MAP_512_512).  These
 * addresses match the Espressif SDK examples for SPI_FLASH_SIZE_MAP == 4.
 *
 * RF_CAL and PHY_DATA addresses are identical for maps 4 and 6 (both 4MB);
 * only the OTA slot addresses differ.
 * ========================================================================= */
#define PART_OTA_SIZE      0x06A000UL   /* 424 KB — SDK standard for map 4 */
#define PART_OTA2_ADDR     0x081000UL   /* OTA2 at 512 KB + 4 KB           */
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

/* Shared with main.c.  Declared void* so main.c can include a compatible
 * (but ktos.h-safe) struct definition without pulling in SDK headers.   */
void *g_scan_results;

extern void ktos_app_main(void);  /* CALL0 — must be crossed with callx0 */

/* =========================================================================
 * Scan callbacks — windowed ABI, called by the SDK's ets_main() event loop.
 * ========================================================================= */

/* FRC1 control and interrupt-clear registers. */
#define FRC1_CTRL  (*(volatile uint32_t *)0x60000608UL)
#define FRC1_INT   (*(volatile uint32_t *)0x6000060CUL)

static void ICACHE_FLASH_ATTR on_scan_done(void *arg, STATUS status)
{
    if (status == OK) {
        g_scan_results = arg;
    }
    /* Stop FRC1 before abandoning ets_run().  The SDK's FRC1 ISR drives
     * os_timer callbacks; without ets_run() processing events those callbacks
     * may write debug output to UART and corrupt the scan results display. */
    FRC1_CTRL = 0;
    FRC1_INT  = 0;

    /* Stop both watchdog timers before crossing into ktos_app_main.  Both are
     * windowed-ABI functions so they must be called here, not in ktos_app_main.
     * ets_wdt_disable() stops the ROM hardware WDT (~1.6 s timeout fed by
     * ets_run); system_soft_wdt_stop() stops the SDK software WDT.
     * Once we callx0 out of ets_run neither timer is fed again. */
    ets_wdt_disable();
    system_soft_wdt_stop();

    /* Cross windowed → CALL0.  callx0 passes a0=ret_addr without rotating
     * the window; ktos_app_main never returns so the dangling frame is safe. */
    __asm__ volatile ("callx0 %0" :: "r"(ktos_app_main) : "a0", "memory");
    while (1); /* unreachable */
}

static void ICACHE_FLASH_ATTR on_sdk_ready(void)
{
    wifi_station_scan(NULL, on_scan_done);
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
    /* Must be called in user_init() so the mode change takes effect. */
    wifi_set_opmode(STATION_MODE);

    system_init_done_cb(on_sdk_ready);
}
