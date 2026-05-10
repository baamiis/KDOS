# WiFi Connect Example {#example_wifi_connect}

Connects to a WPA2 access point, waits for a DHCP lease, then prints the
assigned IP address, gateway, subnet mask, and RSSI.  The first example that
requires editing a configuration file with your network credentials.

---

## What It Does

1. `startup.c` sets `STATION_MODE`, loads credentials from `wifi_config.h`,
   and calls `wifi_station_connect()` once RF is ready.
2. The SDK fires `EVENT_STAMODE_GOT_IP` when DHCP completes.
3. `on_wifi_event()` saves IP / gateway / mask / RSSI, stops FRC1, disables
   watchdogs, and crosses into CALL0 via `callx0` → `ktos_app_main()`.
4. `ktos_app_main()` posts `MSG_CONNECTED` to `connect_task`.
5. `connect_task` prints the connection details and sleeps.

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

Edit `wifi_config.h` before building:

```c
#define WIFI_SSID     "YourNetwork"
#define WIFI_PASSWORD "YourPassword"
```

---

## Build

```bash
cd examples/esp8266/wifi_connect
make
```

Produces:

```
wifi_connect-0x00000.bin   (IRAM image)
wifi_connect-0x10000.bin   (IROM / XIP image)
```

---

## Flash

```bash
make flash PORT=/dev/ttyUSB0
```

Full flash map:

| Address    | File |
|------------|------|
| `0x00000`  | `wifi_connect-0x00000.bin` |
| `0x10000`  | `wifi_connect-0x10000.bin` |
| `0x3FB000` | `blank.bin` (RF_CAL) |
| `0x3FC000` | `esp_init_data_default_v08.bin` (PHY init) |
| `0x3FD000` | `blank.bin` (system params) |

---

## Expected Output

```
=============================
   KTOS WiFi Connect
=============================
SSID     : YourNetwork
IP       : 192.168.1.42
Gateway  : 192.168.1.1
Mask     : 255.255.255.0
RSSI     : -67 dBm
----
Connected.
```

---

## How It Works

The flow mirrors `wifi_scan` — `ets_run()` drives the SDK until the
association and DHCP exchange complete, then `callx0` permanently hands
control to KTOS.  FRC1 is stopped immediately before the jump so the
`os_timer` ISR does not fire into abandoned SDK state.

`wifi_station_set_auto_connect(0)` is called in `user_init()` so the chip
does not attempt to reconnect to a saved SSID on boot.  The explicit
`wifi_station_connect()` call in `on_sdk_ready()` gives us a clean, single
connection attempt with known credentials.
