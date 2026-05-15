# Hello KTOS — ESP8266 {#example_hello_ktos}

The simplest KTOS example: a single task that prints `Hello from KTOS!` once
per second over UART0 at 74880 8N1.  No WiFi, no SDK dependency in
`main.c` — just the KTOS scheduler running bare on the Xtensa LX106.

---

## What It Does

`startup.c` (windowed ABI) performs the minimal SDK handshake — partition
table registration and `user_init()` — then crosses into the CALL0 world
via a `callx0` instruction.  `ktos_app_main()` initialises UART0, creates
`hello_task`, and calls `ktos_RunOS()`.  The task loops forever printing
the greeting with a 1-second CCOUNT-based delay.

---

## Prerequisites

| Tool | Notes |
|------|-------|
| `xtensa-lx106-elf-gcc` | Xtensa toolchain for LX106 |
| `esptool.py` | Flash utility |
| ESP8266 Non-OS SDK | Expected at `~/esp/sdk` (override with `SDK_DIR=`) |

Serial monitor set to **74880 baud, 8N1**.

---

## Build

```bash
cd examples/esp8266/hello_ktos
make
```

Produces:

```
hello_ktos-0x00000.bin   (IRAM image)
hello_ktos-0x10000.bin   (IROM / XIP image)
```

---

## Flash

```bash
make flash PORT=/dev/ttyUSB0
```

Full flash map written:

| Address   | File |
|-----------|------|
| `0x00000` | `hello_ktos-0x00000.bin` |
| `0x10000` | `hello_ktos-0x10000.bin` |
| `0x3FB000` | `blank.bin` (RF_CAL) |
| `0x3FC000` | `esp_init_data_default_v08.bin` (PHY init) |
| `0x3FD000` | `blank.bin` (system params) |

---

## Expected Output

Open your serial monitor at **74880 baud, 8N1**:

```
=============================
  KTOS on ESP8266 - running!
=============================
Hello from KTOS!
Hello from KTOS!
Hello from KTOS!
...
```

---

## How It Works

The ESP8266 ROM boots in windowed register-window ABI.  KTOS `ktos_app_main()`
is compiled with CALL0 ABI (`-mlongcalls`).  `startup.c` bridges the two
worlds using `callx0 %reg` which jumps without touching the register-window
state, so the stack pointer arrives intact.

`ktos_hal_InitSystemTimer()` on the ESP8266 BSP is a deliberate no-op — the
SDK's FRC1 hardware timer keeps running for its own bookkeeping.  KTOS timing
in this example is done with the Xtensa `CCOUNT` register (CPU cycle counter)
instead, which needs no hardware setup.
