# WiFi Access Point Example {#example_wifi_ap}

Configures the ESP8266 as a soft access point (SoftAP) and prints the AP
SSID, password, channel, and assigned IP address.  The AP stays alive
indefinitely after KTOS finishes printing — other devices can connect to it.

This example demonstrates a different KTOS integration pattern from the
scan/connect/https examples: instead of permanently abandoning `ets_run()`,
KTOS runs briefly inside it and then voluntarily exits so the WiFi stack can
keep servicing beacon frames.

---

## What It Does

1. `startup.c` sets `SOFTAP_MODE`, configures the AP from `wifi_ap_config.h`,
   and registers `system_init_done_cb(on_sdk_ready)`.
2. `on_sdk_ready()` reads the AP IP from `wifi_get_ip_info()`, disables
   watchdogs, and calls `ktos_app_main()` as a **plain function call**
   (not `callx0`).  FRC1 is intentionally left running.
3. `ktos_app_main()` creates `ap_task`, posts `MSG_AP_READY`, and calls
   `ktos_RunOS()`.
4. `ap_task` prints the AP details, calls `ktos_ExitOS()`, and returns.
5. `ktos_RunOS()` returns to `on_sdk_ready()`, which returns to `ets_run()`.
   The AP keeps broadcasting beacons and accepting connections.

---

## Prerequisites

| Tool | Notes |
|------|-------|
| `xtensa-lx106-elf-gcc` | Xtensa toolchain for LX106 |
| `esptool.py` | Flash utility |
| ESP8266 Non-OS SDK | Expected at `~/esp/sdk` (override with `SDK_DIR=`) |

Serial monitor set to **74880 baud, 8N1**.

---

## Configuration

Edit `wifi_ap_config.h` before building:

```c
#define AP_SSID     "KTOS-AP"
#define AP_PASSWORD "ktos1234"
#define AP_CHANNEL  6
```

The password must be at least 8 characters for WPA2.  Set `AP_CHANNEL` to
a channel with low interference in your environment (1, 6, or 11 recommended).

---

## Build

```bash
cd examples/esp8266/wifi_AP
make
```

Produces:

```
wifi_AP-0x00000.bin   (IRAM image)
wifi_AP-0x10000.bin   (IROM / XIP image)
```

---

## Flash

```bash
make flash PORT=/dev/ttyUSB0
```

Full flash map:

| Address    | File |
|------------|------|
| `0x00000`  | `wifi_AP-0x00000.bin` |
| `0x10000`  | `wifi_AP-0x10000.bin` |
| `0x3FB000` | `blank.bin` (RF_CAL) |
| `0x3FC000` | `esp_init_data_default_v08.bin` (PHY init) |
| `0x3FD000` | `blank.bin` (system params) |

---

## Expected Output

```
=============================
   KTOS WiFi AP
=============================
SSID     : KTOS-AP
Password : ktos1234
Channel  : 6
Auth     : WPA2
IP       : 192.168.4.1
----
AP ready - connect to KTOS-AP
```

After printing, the device is silent on UART but the AP remains visible and
connectable.  The SDK default IP for a SoftAP is `192.168.4.1`.

---

## How It Works

### Why not callx0?

The scan, connect, and https_get examples use `callx0` to permanently abandon
`ets_run()` once the network operation is complete.  An AP cannot do this:
the WiFi stack needs `ets_run()` running indefinitely to send beacon frames
every 100 ms.  If `ets_run()` is abandoned, the AP disappears from scan
results within seconds.

### ktos_ExitOS()

`ktos_ExitOS()` sets an internal flag checked at the top of every scheduler
loop iteration.  When `ap_task` calls it and returns, the scheduler sees the
flag and returns from `ktos_SwitchTask()`, which causes `ktos_RunOS()` to
return, which causes `on_sdk_ready()` to return, which gives control back to
`ets_run()`.

### FRC1

The SDK's `os_timer` subsystem runs on the FRC1 hardware timer.  WiFi beacon
management is implemented as an `os_timer` callback.  This example leaves FRC1
running; `ktos_hal_InitSystemTimer()` on the ESP8266 BSP is a no-op for exactly
this reason.
