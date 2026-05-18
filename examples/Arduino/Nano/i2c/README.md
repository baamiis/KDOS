# KTOS — Arduino Nano I²C Scanner Example (Bare-Metal)

A **single KTOS task** walks the I²C bus from `0x01` to `0x7E` once on
startup, then every 5 seconds, reporting every responding address.

KTOS is the operating system.  TWI and USART0 are driven directly
through ATmega328P registers — no Arduino framework, no `Wire`.

---

## Overview

```
              ┌─────────────────────────────────────┐
              │           scanner_task              │
              ├─────────────────────────────────────┤
INIT  ───────►│ print banner;                       │
              │ run_scan();                         │
              │                                     │
TIMER ◄──┐    │ run_scan();                         │
   5 s   │    └──────────────┬──────────────────────┘
         │                   │
         └── return 5000; ───┘
```

`run_scan()` issues a START / SLA+W / STOP transaction for each address
and reports the ones that ACK.

---

## What it shows about KTOS

- Long-period (5 s) and short-period tasks are interchangeable — return
  any value from `1` to `65534` ms.
- A single task can do meaningful work without any other infrastructure:
  no `loop()`, no `millis()` polling, no global timer state.
- `KTOS_MSG_TYPE_INIT` runs exactly once; every subsequent wake is
  `KTOS_MSG_TYPE_TIMER`.

---

## Hardware Required

| Item                                                 | Quantity |
|------------------------------------------------------|----------|
| Arduino Nano Classic                                 | 1        |
| Any I²C device (OLED, RTC, EEPROM, IMU, …)           | ≥ 1      |
| 4.7 kΩ pull-up resistors (only if your module lacks them) | 0–2 |
| Breadboard + jumper wires                            | —        |

Most breakout boards (SSD1306, DS3231, BME280, MPU-6050, …) already
include pull-ups, so external resistors are rarely needed.  The example
also enables the ATmega328P internal pull-ups on PC4/PC5 as a weak
fallback.

---

## Wiring

| Device pin | Nano label | AVR pin |
|------------|------------|---------|
| `SDA`      | `A4`       | `PC4`   |
| `SCL`      | `A5`       | `PC5`   |
| `VCC`      | `5V`       | —       |
| `GND`      | `GND`      | —       |

```
   Nano A4 (PC4) ── SDA ── device
   Nano A5 (PC5) ── SCL ── device
   Nano 5V       ── VCC ── device
   Nano GND      ── GND ── device
```

> **Pull-ups:** TWI lines are open-drain.  If your module doesn't have
> built-in pull-ups, add 4.7 kΩ resistors from each line to `5V`.

> **3.3 V devices:** the Nano drives the bus at 5 V.  Use a level
> shifter for parts that are not 5 V-tolerant.

---

## How It Works

1. `main()` calls `uart_init()` (USART0 at 115200 baud, 8N1) and
   `twi_init()` (TWBR = 72 → 100 kHz at 16 MHz F_CPU; prescaler = 1).
2. `ktos_InitTask(scanner_task, 96, 4, 'I')` allocates a 384-byte stack,
   a 4-message queue, and a TCB.
3. `ktos_RunOS()` configures Timer1 CTC for 1 ms (via the BSP) and
   transfers control to `scanner_task` with `KTOS_MSG_TYPE_INIT`.
4. The task prints the banner and runs the first scan.  Each address
   probe is a START → write `(addr << 7) | 0` → STOP transaction; the
   probe is "found" if `TWSR` reports `SLA+W ACK` (`0x18`).
5. `scanner_task` returns `5000`.  KTOS suspends it.
6. The Timer1 ISR ticks the task's countdown.  After 5000 ticks the
   scheduler redispatches the task with `KTOS_MSG_TYPE_TIMER`.

The scan repeats every 5 seconds forever.

---

## Download, Build, and Flash

### 1. Prerequisites

- **PlatformIO** — install the [VS Code extension](https://platformio.org/install/ide?install=vscode), or the CLI:
  ```bash
  python3 -m pip install --user platformio
  pio --version
  ```
- **USB-serial driver** — most Nano *clones* use the CH340 chip and need the
  [WCH CH340 driver](https://www.wch.cn/downloads/CH341SER_EXE.html) on Windows / macOS.
  Official Nanos with an FTDI chip work out of the box.

### 2. Get the source

```bash
git clone https://github.com/baamiis/KTOS.git
cd KTOS/examples/Arduino/Nano/i2c
```

(Or download the repository as a ZIP from GitHub and `cd` into the same folder.)

### 3. Connect the Nano and find its port

Plug the Nano into your computer with a USB cable, then:

```bash
pio device list
```

Look for an entry whose description contains `USB Serial`, `FT232`, or `CH340`.
Typical port names:

| OS       | Example port                     |
|----------|----------------------------------|
| Linux    | `/dev/ttyUSB0` or `/dev/ttyACM0` |
| macOS    | `/dev/cu.usbserial-1410`         |
| Windows  | `COM4`                           |

PlatformIO auto-detects the port in most cases — note it down only if
upload fails to find it.

### 4. Build

```bash
pio run
```

The first build downloads `toolchain-atmelavr` (~30 MB, one time only).
Subsequent builds finish in a few seconds.  PlatformIO automatically
pulls `core/ktos.c` and `bsp/atmega328p/ktos_bsp.c` into the build via
`build_src_filter`.  Output lands at `.pio/build/nanoatmega328/firmware.elf`
(and `.hex`).

### 5. Flash

```bash
pio run -t upload
```

PlatformIO talks to the Nano's USB bootloader via `avrdude -c arduino`
(STK500v1) — no buttons to press, no jumpers to move.  If auto-detection
fails, pass the port explicitly:

```bash
pio run -t upload --upload-port COM4          # Windows
pio run -t upload --upload-port /dev/ttyUSB0  # Linux / macOS
```

### 6. Open the serial monitor

```bash
pio device monitor -b 115200
```

Press the Nano's **reset** button if no output appears.  Quit the
monitor with `Ctrl+C` (CLI) or close the panel (VS Code).

> **Only one process can hold the USB port at a time.**  Close the
> monitor before re-running `pio run -t upload`.

---

## Expected Output

With an SSD1306 OLED display connected:

```
=============================
  KTOS I2C Scanner Example
=============================
Scanning I2C bus...
Device found at 0x3C
Scan complete (1 device).
```

With two devices (e.g. SSD1306 OLED + DS3231 RTC):

```
Scanning I2C bus...
Device found at 0x3C
Device found at 0x68
Scan complete (2 devices).
```

With nothing connected:

```
Scanning I2C bus...
No I2C devices found.
```

The scan repeats every 5 seconds, so you can hot-plug devices and
watch them appear on the next pass.

---

## Troubleshooting

| Symptom                                  | Likely cause                                        | Fix                                                       |
|------------------------------------------|-----------------------------------------------------|-----------------------------------------------------------|
| Every address `0x01..0x7E` shows present | SDA/SCL swapped or shorted to GND                  | Re-check wiring; SDA = A4, SCL = A5                       |
| `No I2C devices found.`                  | Missing pull-ups, wrong VCC, or device not powered  | Add 4.7 kΩ pull-ups, confirm 5 V on the module             |
| Scan hangs, board never reboots          | Bus held low by a faulty device                    | Disconnect modules one-by-one to isolate                  |
| Banner prints but scan never repeats     | Timer1 not feeding KTOS                            | Confirm `ISR(TIMER1_COMPA_vect)` is in `main.c`           |
| First scan never returns                 | TWI peripheral disabled                            | Confirm `TWCR & TWEN` is set in `twi_init()`              |

---

## Notes for KDOS Integration

- `twi_init()` / `twi_probe()` will become the kernel of
  `ktos_hal_i2c_init()` / `ktos_hal_i2c_probe()`.
- The 5 s schedule is already a clean KTOS pattern — no work needed
  when the I²C HAL lands.
- The KDOS I²C HAL will expose 7-bit addresses (no shifted/RW-encoded
  values), which matches the convention this example already uses.
