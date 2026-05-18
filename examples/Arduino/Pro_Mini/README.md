### KTOS on Arduino Pro Mini — Examples

KTOS bare-metal examples for the **Arduino Pro Mini**.  Same MCU as the
Nano and the Uno (**ATmega328P**, 16 MHz, 2 KB SRAM, 32 KB flash), same
KTOS BSP, same source code — the difference is that the Pro Mini has
**no on-board USB-serial chip**.  You program it through an external
**USB-TTL adapter** (FTDI FT232 or any CH340-based clone).

---

## Pro Mini variants

The Pro Mini ships in two variants.  Pick the right `board =` line in
each example's `platformio.ini`:

| Variant         | Voltage | Clock  | `board =` line          | Source change |
|-----------------|---------|--------|--------------------------|---------------|
| **5 V**         | 5 V     | 16 MHz | `pro16MHzatmega328` (set by default in this folder) | none |
| **3.3 V**       | 3.3 V   | 8 MHz  | `pro8MHzatmega328`       | bump `UBRR0L = 16` → `UBRR0L = 8` in `uart_init()` so 115 200 baud stays correct at the lower clock |

> The KTOS BSP's Timer1 OCR1A constant (`249`) gives a 1 ms tick at
> 16 MHz; on the 8 MHz variant change it to `124` in
> `bsp/atmega328p/ktos_bsp.c` (or sit on a 2 ms tick if you prefer to
> leave the BSP alone).

---

## Examples

| Folder                                          | Demonstrates                                                       |
|-------------------------------------------------|--------------------------------------------------------------------|
| [`adc/`](adc/README.md)                         | Single KTOS task, 1 Hz timer wake — `return 1000;`                  |
| [`i2c/`](i2c/README.md)                         | Single KTOS task, 5 s timer wake doing TWI bus scan                 |
| [`spi/`](spi/README.md)                         | Single KTOS task, hardware SPI master loopback (MOSI ↔ MISO jumper) |
| [`led_control/`](led_control/README.md)         | **Two tasks**, `ktos_SendMsg(MSG_SET_MODE)` from UART to LED        |
| [`button_control/`](button_control/README.md)   | **Two tasks**, debouncer publishes `MSG_BUTTON_EVENT` to a UI task  |
| [`uart/`](uart/README.md)                       | **Two tasks**, line-assembler hands lines to a command processor    |

The `adc / i2c / led_control / button_control / uart` source files are
line-for-line identical to the [Nano examples](../Nano/); only the
`board = …` line in `platformio.ini` differs.  The Uno-specific
examples ([`usb_serial/`](../Uno/usb_serial/README.md) and
[`eeprom/`](../Uno/eeprom/README.md)) also run on the Pro Mini with the
same one-line board change — no separate copies needed.

The **`spi/`** example is Pro Mini-first: it uses the ATmega328P's
hardware SPI peripheral (`SPCR/SPSR/SPDR`) for a loopback self-test
and works equally on the Nano and Uno with `board = nanoatmega328` /
`board = uno`.

---

## Required Hardware

| Item                                              | Notes                                                  |
|---------------------------------------------------|--------------------------------------------------------|
| Arduino Pro Mini (5 V/16 MHz or 3.3 V/8 MHz)      | This README assumes the 5 V variant.                   |
| **USB-TTL adapter**                               | FTDI FT232RL board, CH340 board, or similar.  Must have a **DTR** pin for auto-reset. |
| 6 jumper wires                                    | For the programming header.                            |

If your adapter does **not** expose `DTR`, you'll need to tap `RST` to
ground for ~50 ms whenever PlatformIO says
*"avrdude: stk500_recv(): programmer is not responding"* — the Pro
Mini's optiboot only listens for ~700 ms after a reset.

---

## Wiring the USB-TTL Adapter

The Pro Mini exposes a 6-pin programming header (often labelled
`BLK / GND / VCC / RXI / TXO / GRN` or `DTR / TX / RX / VCC / GND / GND`
depending on the silkscreen vendor).  Wire it to the adapter as:

| USB-TTL adapter | Pro Mini header pin |
|-----------------|---------------------|
| `GND`           | `GND`               |
| `VCC` (5 V)     | `VCC` (or `RAW` on some boards — match your variant) |
| `TX`            | `RX`  (TXO on Pro Mini = the MCU's USART0 TX) |
| `RX`            | `TX`  (RXI on Pro Mini = the MCU's USART0 RX) |
| `DTR`           | `DTR` (the leftmost pin on most Pro Mini headers) |

```
  USB-TTL                Pro Mini
  ┌──────┐               ┌──────┐
  │ GND  │ ───── GND ────│ GND  │
  │ VCC  │ ───── 5V  ────│ VCC  │       (use 3V3 if your adapter and
  │ RX   │ ────── ─── ───│ TX   │        Pro Mini are 3.3 V variants)
  │ TX   │ ────── ─── ───│ RX   │
  │ DTR  │ ────── ─── ───│ DTR  │       (mandatory for auto-reset)
  └──────┘               └──────┘
```

> **TX ↔ RX cross-over** is intentional: the adapter's TX drives the
> Pro Mini's RX and vice versa.

> **DTR is not optional** for the canonical programming flow.  PlatformIO
> pulses DTR low to trigger the bootloader the same way the Arduino IDE
> does.  Boards without a DTR pin can still program if you press the
> Pro Mini's RST button at exactly the right moment, but it's painful.

---

## Voltage matching

The Pro Mini is a **5 V** board (`pro16MHzatmega328`) **or** a **3.3 V**
board (`pro8MHzatmega328`).  You **must** match your adapter's logic
level to the board:

| Pro Mini | Adapter   | OK?           |
|----------|-----------|---------------|
| 5 V      | 5 V       | ✓ recommended |
| 3.3 V    | 3.3 V     | ✓ recommended |
| 3.3 V    | 5 V       | ⚠ Tolerable on the data lines (5 V signals into 3.3 V inputs *can* damage the MCU over time — use a 1 kΩ series resistor on TX/RX or set the adapter to 3.3 V). |
| 5 V      | 3.3 V     | The bootloader may not see the adapter's 3.3 V logic reliably. |

Most FTDI/CH340 adapters have a 5 V / 3.3 V jumper — set it before
plugging the Pro Mini in.

---

## Common Setup

### 1. Install PlatformIO

VS Code extension, or CLI:

```bash
python3 -m pip install --user platformio
pio --version
```

### 2. Install the USB-TTL driver if needed

- **CH340** boards: install the [WCH CH340 driver](https://www.wch.cn/downloads/CH341SER_EXE.html) on Windows/macOS.
- **FTDI FT232** boards: drivers ship with modern Linux/macOS; on Windows download from [FTDI](https://ftdichip.com/drivers/vcp-drivers/).

### 3. Open an example

```bash
cd KTOS/examples/Arduino/Pro_Mini/adc       # or any other
pio run                                     # compile
pio run -t upload                           # flash via USB-TTL adapter
pio device monitor -b 115200                # open serial console
```

PlatformIO auto-detects the adapter port in most cases.  If not, pass
it explicitly:

```bash
pio run -t upload --upload-port COM4          # Windows
pio run -t upload --upload-port /dev/ttyUSB0  # Linux / macOS
```

---

## Board Overview

| Item              | 5 V / 16 MHz             | 3.3 V / 8 MHz            |
|-------------------|--------------------------|---------------------------|
| Board             | Arduino Pro Mini 328 (5V) | Arduino Pro Mini 328 (3V3)|
| MCU               | ATmega328P                | ATmega328P                |
| Clock             | 16 MHz                    | 8 MHz                     |
| Logic level       | 5 V                       | 3.3 V                     |
| Flash             | 32 KB                     | 32 KB                     |
| SRAM              | 2 KB                      | 2 KB                      |
| EEPROM            | 1 KB                      | 1 KB                      |
| KTOS BSP          | [`bsp/atmega328p/`](../../../bsp/atmega328p/) | same         |
| KTOS tick source  | Timer1 CTC, 1 ms          | Timer1 CTC, 1 ms (OCR1A = 124) |
| Upload            | External USB-TTL adapter  | External USB-TTL adapter  |
| Upload speed      | 57 600 baud (legacy bootloader, set in `platformio.ini`) | 57 600 baud |
| PlatformIO board  | `pro16MHzatmega328`       | `pro8MHzatmega328`        |

---

## Troubleshooting

| Symptom                                                | Likely cause                            | Fix                                                                          |
|--------------------------------------------------------|------------------------------------------|-------------------------------------------------------------------------------|
| `avrdude: stk500_recv(): programmer is not responding` | Adapter lacks DTR or RST timing is off  | Wire DTR — or hold RST low, run `pio run -t upload`, release RST when avrdude says *"Connecting..."* |
| Adapter not detected by `pio device list`              | Missing driver                           | Install CH340 or FTDI driver (see step 2)                                     |
| Garbled serial output                                  | Wrong baud rate                          | Set monitor to **115 200**, 8N1                                               |
| Garbled serial output (3.3 V board only)               | UBRR0 still set for 16 MHz at 115 200    | Change `UBRR0L = 16` → `UBRR0L = 8` in `uart_init()` and rebuild              |
| Programming works but app misbehaves                   | Logic-level mismatch                     | Match adapter voltage to Pro Mini variant                                     |
| Tasks fire too slow (3.3 V variant)                    | Timer1 OCR1A still set for 16 MHz tick   | Either rebuild BSP with `OCR1A = 124` or accept the 2 ms tick                 |
