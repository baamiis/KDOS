# KTOS — Arduino Pro Mini SPI Loopback Example (Bare-Metal)

A **single KTOS task** drives the ATmega328P's hardware SPI peripheral
in master mode and runs a loopback self-test once per second.  Jumper
**MOSI ↔ MISO** with a single wire and watch every byte echo back.

KTOS is the operating system — no Arduino framework, no `SPI.h`.
Direct register access for `SPCR`, `SPSR`, `SPDR`, plus `DDRB`/`PORTB`
for the chip-select line.

---

## Overview

```
              ┌─────────────────────────────────────────────┐
              │                  spi_task                   │
              ├─────────────────────────────────────────────┤
INIT  ───────►│ banner; spi_master_init(); run_loopback_test()
              │                                             │
TIMER ◄──┐    │ for each byte in TEST_PATTERN:              │
   1 s   │    │   out = byte                                │
         │    │   in  = spi_transfer(out)                   │
         │    │   if in != out: count mismatch              │
         │    │ print "PASS" / "FAIL n/8 mismatches"        │
         │    └──────────────┬──────────────────────────────┘
         │                   │
         └── return 1000; ───┘
```

The 8-byte test pattern (`0xAA 0x55 0xFF 0x00 0x12 0x34 0xDE 0xAD`) is
designed to detect every kind of single-bit fault: alternating bits,
all-ones, all-zeros, and a few asymmetric values that catch byte-order
or MSB/LSB issues.

---

## What it shows about KTOS

- A periodic task driving a real peripheral — `return 1000;` gives the
  scheduler full control of pacing.
- Bare-metal SPI in roughly 10 lines of register code; KTOS doesn't
  need a driver layer to do useful work.
- Pull the jumper while it's running — the task keeps running and
  starts printing `FAIL` lines.  KTOS doesn't crash; you see the
  failure surface as application output.

---

## Hardware Required

| Item                                            | Quantity |
|-------------------------------------------------|----------|
| Arduino Pro Mini (5 V / 16 MHz variant)         | 1        |
| USB-TTL adapter (FTDI / CH340)                  | 1        |
| 1 jumper wire                                   | 1        |

No external SPI device is required — the test relies on shorting MOSI
back to MISO so the master receives whatever it sends.

---

## Wiring

For programming + serial output, follow the standard Pro Mini
[USB-TTL wiring in the parent README](../README.md#wiring-the-usb-ttl-adapter).

For the SPI loopback test, add **one** jumper:

| From       | To         | Note                              |
|------------|------------|-----------------------------------|
| `D11`/MOSI | `D12`/MISO | The whole test is in this jumper. |

```
   Pro Mini header:
   ──── D10 (SS, output, asserted low during transfer)
   ──── D11 (MOSI) ───┐
   ──── D12 (MISO) ───┘ jumper
   ──── D13 (SCK, also on-board LED)
```

> **The on-board LED will flicker during each transfer.**  SCK lives
> on `PB5` = `D13` = the LED pin; that's an ATmega328P fact, not a
> KTOS bug.  The LED is unavailable for other purposes while SPI is
> active.

> **SS must be driven (it is).** The example configures `PB2`/`D10` as
> an output and idles it high.  If `SS` is left as an input and an
> external low pulse appears on it, the SPI hardware silently switches
> the AVR into slave mode.

---

## How It Works

1. `main()` initialises USART0 (115 200 8N1) and the SPI peripheral,
   then registers one task and calls `ktos_RunOS()`.

2. `spi_master_init()` configures the SPI:
   - `DDRB`: MOSI, SCK, SS as outputs; MISO as input.
   - `PORTB`: SS idle high.
   - `SPCR = (1<<SPE) | (1<<MSTR) | (1<<SPR0)` → SPI enabled, master,
     mode 0 (CPOL=0, CPHA=0), MSB-first, F_osc/16 = **1 MHz** at
     16 MHz F_CPU.
   - `SPSR = 0` → no double-speed.

3. On every dispatch (`INIT` or `TIMER`) the task calls
   `run_loopback_test()`:
   - Drop SS low (begin transaction).
   - For each byte in `TEST_PATTERN`: write `SPDR`, busy-wait on
     `SPSR & (1<<SPIF)`, read the same register to get the byte the
     hardware shifted in.
   - Compare in == out and accumulate any mismatches.
   - Raise SS high (end transaction).
   - Print `PASS  (8/8 bytes echoed)` or `FAIL n/8 mismatches; first
     at byte X sent 0xYY got 0xZZ`.

4. Task returns `1000`; the KTOS scheduler suspends it until the next
   `KTOS_MSG_TYPE_TIMER` arrives 1 s later.

The whole SPI driver is fewer than 20 lines.  Adding an SPI sensor or
flash chip is a matter of swapping `run_loopback_test()` for the
chip-specific transaction.

---

## Download, Build, and Flash

### 1. Prerequisites

- **PlatformIO** — install the [VS Code extension](https://platformio.org/install/ide?install=vscode), or the CLI:
  ```bash
  python3 -m pip install --user platformio
  pio --version
  ```
- **USB-TTL driver** — CH340 needs the [WCH driver](https://www.wch.cn/downloads/CH341SER_EXE.html);
  FT232 boards work out of the box on every modern OS.

### 2. Get the source

```bash
git clone https://github.com/baamiis/KTOS.git
cd KTOS/examples/Arduino/Pro_Mini/spi
```

### 3. Wire the Pro Mini

- USB-TTL adapter to the 6-pin Pro Mini header (DTR ↔ DTR, TX ↔ RX, RX ↔ TX, VCC, GND, GND).
- One jumper between `D11` and `D12`.

### 4. Connect, build, flash

```bash
pio device list                  # find the adapter port
pio run                          # compile
pio run -t upload                # flash (avrdude via adapter, 57 600 baud)
pio device monitor -b 115200     # open the serial console
```

If auto-detect fails:

```bash
pio run -t upload --upload-port COM4          # Windows
pio run -t upload --upload-port /dev/ttyUSB0  # Linux / macOS
```

---

## Expected Output

With the MOSI ↔ MISO jumper in place:

```
=============================
  KTOS SPI Loopback Example
=============================
Jumper D11 (MOSI) <-> D12 (MISO) and watch each
iteration echo 8/8 bytes.  Pull the jumper to see FAIL.
Iter 1: PASS  (8/8 bytes echoed)
Iter 2: PASS  (8/8 bytes echoed)
Iter 3: PASS  (8/8 bytes echoed)
...
```

Pull the jumper out mid-stream — every byte sent gets clocked into a
floating MISO, so the receiver typically reads `0xFF` (idle bus
pull-up) or `0x00`:

```
Iter 11: FAIL  7/8 mismatches; first at byte 0 sent 0xAA got 0xFF
Iter 12: FAIL  7/8 mismatches; first at byte 0 sent 0xAA got 0xFF
```

Re-insert the jumper and `PASS` returns on the next iteration.

---

## Troubleshooting

| Symptom                                    | Likely cause                                       | Fix                                                                  |
|--------------------------------------------|----------------------------------------------------|----------------------------------------------------------------------|
| Always `FAIL` even with jumper installed   | Jumper on wrong pins / loose contact               | Double-check MOSI = `D11/PB3`, MISO = `D12/PB4`                      |
| Always `FAIL 8/8 mismatches got 0x00`      | `SS` left floating and pulled low externally       | Confirm `DDRB |= (1<<PORTB2)` in `spi_master_init()`                 |
| Garbled UART, SPI seems unrelated          | UBRR0 wrong for the variant                        | 8 MHz Pro Mini needs `UBRR0L = 8` (this file uses `16` for 16 MHz)   |
| LED on D13 lit constantly                  | Not an error — SCK idles low between transfers and a long burst can leave the LED visibly on briefly | Ignore; LED is the SCK pin |
| Banner prints once, nothing after          | Timer1 ISR not hooked up                           | Confirm `ISR(TIMER1_COMPA_vect)` is present in `main.c`              |
| `[KTOS FATAL] T Failed` on boot            | Heap exhausted creating the task                   | Drop `StackSize` argument from 96 to 64                              |

---

## Notes for KDOS Integration

- `spi_master_init()` / `spi_transfer()` map directly to a future
  `ktos_hal_spi_init()` / `ktos_hal_spi_transfer()` once the KTOS
  peripheral HAL lands.
- The CS line should be exposed as a `ktos_hal_gpio_*` call so
  applications can address multiple SPI slaves on one bus.
- A higher-throughput variant would replace the busy-wait on `SPIF`
  with an `SPI_STC_vect` ISR that posts `MSG_BYTE_DONE` to the SPI
  task — but at 1 MHz × 8 bits = 8 µs per byte, the polled version
  costs less CPU than the ISR overhead.

---

## Adapting the example

| To do…                                            | Change                                                                       |
|---------------------------------------------------|------------------------------------------------------------------------------|
| Run on the 5 V Nano or Uno                        | Edit `platformio.ini`: `board = nanoatmega328` or `board = uno`              |
| Run on the 3.3 V / 8 MHz Pro Mini                 | `board = pro8MHzatmega328` plus `UBRR0L = 8` in `uart_init()`                |
| Drop SPI clock to 250 kHz                         | `SPCR \|= (1<<SPR1) \| (1<<SPR0)`  (F_osc / 64)                              |
| Bump SPI clock to 4 MHz                           | `SPCR &= ~(1<<SPR0)`  + `SPSR \|= (1<<SPI2X)`  (F_osc / 4)                   |
| Talk to a real SPI device (e.g. 25LC256 EEPROM)   | Replace `run_loopback_test()` with `spi_cs_low(); spi_transfer(CMD); ...; spi_cs_high();`  |
