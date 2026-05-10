# WiFi Scan Example {#example_wifi_scan}

Scans for all visible 802.11 networks and prints a formatted table of SSID,
channel, RSSI, and authentication mode.  Demonstrates handing data from the
ESP8266 Non-OS SDK event world into the KTOS cooperative scheduler via a
message.

---

## What It Does

1. `startup.c` sets `STATION_MODE` and calls `wifi_station_scan()`.
2. The SDK fires `on_scan_done()` with a linked list of `bss_info` results.
3. `on_scan_done()` stops FRC1, disables both watchdogs, and crosses into
   CALL0 via `callx0` → `ktos_app_main()`.  `ets_run()` is abandoned.
4. `ktos_app_main()` posts `MSG_SCAN_DONE` (carrying the `bss_info` pointer)
   to `wifi_task`, then starts the scheduler.
5. `wifi_task` receives the message and prints the BSS table.

---

## Prerequisites

| Tool | Notes |
|------|-------|
| `xtensa-lx106-elf-gcc` | Xtensa toolchain for LX106 |
| `esptool.py` | Flash utility |
| ESP8266 Non-OS SDK | Expected at `~/esp/sdk` (override with `SDK_DIR=`) |

Serial monitor set to **74880 baud, 8N1**.  No configuration file to edit —
the scan is passive and requires no credentials.

---

## Build

```bash
cd examples/esp8266/wifi_scan
make
```

Produces:

```
wifi_scan-0x00000.bin   (IRAM image)
wifi_scan-0x10000.bin   (IROM / XIP image)
```

---

## Flash

```bash
make flash PORT=/dev/ttyUSB0
```

Full flash map:

| Address    | File |
|------------|------|
| `0x00000`  | `wifi_scan-0x00000.bin` |
| `0x10000`  | `wifi_scan-0x10000.bin` |
| `0x3FB000` | `blank.bin` (RF_CAL) |
| `0x3FC000` | `esp_init_data_default_v08.bin` (PHY init) |
| `0x3FD000` | `blank.bin` (system params) |

---

## Expected Output

```
=============================
   KTOS WiFi Scan
=============================
 #   SSID                             CH   RSSI  AUTH
---  -------------------------------- ---  ----  ------
  1  HomeNetwork                        6   -67  WPA2
  2  GuestNet                           1   -72  OPEN
  3  Office5G                          11   -81  WPA/2
----
Scan complete — 3 networks found.
```

---

## How It Works

### FRC1 and ets_run()

The ESP8266 WiFi stack is driven by `ets_run()`, a blocking ROM function that
processes hardware events.  Once the scan result callback fires, WiFi is no
longer needed.  `on_scan_done()` writes `FRC1_CTRL = 0` and `FRC1_INT = 0`
to stop the SDK's `os_timer` ISR, then disables both watchdogs before the
`callx0` jump.  Without stopping FRC1 first, the timer ISR would fire into
dead code and corrupt UART output.

### ABI boundary

`startup.c` is compiled windowed ABI; `main.c` / `ktos.c` are compiled CALL0
(`-mlongcalls`).  The `callx0` instruction crosses the boundary without
disturbing the register-window state, delivering a clean stack to KTOS.

### bss_info layout

`main.c` deliberately avoids including `user_interface.h` to prevent a `bool`
type conflict with `<stdbool.h>`.  Instead it declares a local `struct bss_info`
whose field offsets exactly match the SDK structure at the Xtensa LX106
ABI alignment rules.
