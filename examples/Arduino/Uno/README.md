### KTOS on Arduino Uno R3 — Examples

KTOS bare-metal examples for the **Arduino Uno R3**.  KTOS replaces the
Arduino OS — every example provides its own `main()`, drives peripherals
through direct ATmega328P register access, and hands the CPU to
`ktos_RunOS()` which never returns.

---

## All Nano examples also run on the Uno

The Uno R3 uses the same **ATmega328P** MCU as the Arduino Nano with
identical pin mappings on the bottom-row digital and analog pins.  All
five canonical KTOS examples in [`../Nano/`](../Nano/) compile and run
unmodified on the Uno — you only need to change one line in
`platformio.ini`:

```ini
; was: board = nanoatmega328
board = uno
```

That's it.  Source files, wiring tables, expected output, and the KTOS
BSP are all identical.

| Nano example                                                     | Runs on Uno? | What changes |
|------------------------------------------------------------------|--------------|--------------|
| [`adc/`](../Nano/adc/README.md)                                  | Yes          | `board = uno` |
| [`i2c/`](../Nano/i2c/README.md)                                  | Yes          | `board = uno` |
| [`led_control/`](../Nano/led_control/README.md)                  | Yes          | `board = uno` |
| [`button_control/`](../Nano/button_control/README.md)            | Yes          | `board = uno` |
| [`uart/`](../Nano/uart/README.md)                                | Yes          | `board = uno` |

> Practical tip: keep one PlatformIO project per board.  Copy
> `../Nano/<example>/` into `Uno/<example>/` and flip the board line —
> you do not need to maintain a separate fork of the source.

---

## Uno-specific examples (in this folder)

Two examples showcase capabilities that are most natural to demonstrate
on the Uno:

| Example                                  | What it demonstrates                                                                 |
|------------------------------------------|--------------------------------------------------------------------------------------|
| [`usb_serial/`](usb_serial/README.md)    | Interactive **CLI over USB CDC** — `HELP`, `INFO`, `LED ON/OFF/TOGGLE`, `ADC`, `BTN`, `ECHO` |
| [`eeprom/`](eeprom/README.md)            | Internal **1 KB EEPROM** — boot counter that survives power cycles + `READ`/`WRITE`/`DUMP` shell |

Both use the same two-task pattern (`rx_task` + worker task) you saw
in the Nano `uart` example.

---

## Why the Uno is the natural home for these two

- **USB CDC bridge**: the Uno's on-board **ATmega16U2** runs a USB CDC
  class implementation that bridges to USART0 on the application MCU.
  Plugging the Uno into a host enumerates a real USB device (VID `2341`,
  PID `0043` for genuine boards), not a generic USB-serial chip — and
  the 16U2's firmware can itself be reflashed (DFU mode) to turn the
  Uno into other USB device classes.  The `usb_serial` example uses
  this link as a control plane for GPIO, ADC, and digital input.
- **EEPROM**: every ATmega328P-based board has 1 KB of internal EEPROM,
  but the Uno's stable ICSP layout and standard pin mapping make it the
  canonical place to show how to use it.

Both examples will compile and run on the Nano and Pro Mini as well —
the demonstration just happens to make the most pedagogical sense on
the Uno.

---

## Board Overview

| Item              | Value                                  |
|-------------------|----------------------------------------|
| Board             | Arduino Uno R3                         |
| MCU               | ATmega328P (AVR 8-bit)                 |
| Clock             | 16 MHz                                 |
| Logic level       | 5 V                                    |
| Flash             | 32 KB (≈30 KB after optiboot)          |
| SRAM              | 2 KB                                   |
| EEPROM            | 1 KB                                   |
| KTOS BSP          | [`bsp/atmega328p/`](../../../bsp/atmega328p/) |
| KTOS tick source  | Timer1 CTC, 1 ms                       |
| USB-serial bridge | ATmega16U2 (USB CDC class)             |
| Bootloader        | `optiboot`, 115 200 baud               |
| PlatformIO board  | `uno`                                  |

---

## Common Setup

Identical to the Nano — see the per-example READMEs for the full
"Download, Build, and Flash" walkthrough.  Quick recap:

```bash
git clone https://github.com/baamiis/KTOS.git
cd KTOS/examples/Arduino/Uno/usb_serial   # or eeprom
pio run                                   # build
pio run -t upload                         # flash
pio device monitor -b 115200              # open the CLI
```

No `board_upload.speed` override needed — the Uno's optiboot bootloader
runs at 115 200 baud natively.
