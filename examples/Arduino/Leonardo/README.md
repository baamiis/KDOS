### KTOS on Arduino Leonardo — Bare-Metal Examples

Six examples that run on the **Arduino Leonardo (ATmega32U4)** with
**KTOS as the operating system** — no Arduino framework, no Arduino
runtime, no `setup()` / `loop()`.

---

## Important: Serial output requires a USB-to-serial adapter

The ATmega32U4 has **native USB hardware** — there is no separate
USB-to-serial chip (no FTDI, no CH340).  Without the Arduino USB CDC
stack, the USB port cannot be used for serial communication in
bare-metal mode.

All examples in this folder use **USART1** — the hardware UART exposed
on the digital header at **D0 (RX, PD2)** and **D1 (TX, PD3)**.

**Required wiring (any USB-to-serial adapter at 3.3 V or 5 V):**

```
adapter TX  →  Leonardo D0 (RX / PD2)
adapter RX  →  Leonardo D1 (TX / PD3)
adapter GND →  Leonardo GND
```

Open your terminal at **115200 baud, 8N1**.

---

## Examples

| Example                                         | KTOS feature exercised                                         |
|-------------------------------------------------|----------------------------------------------------------------|
| [`uart/`](uart/README.md)                       | **Two tasks**, line-assembler hands lines to command processor |
| [`led_control/`](led_control/README.md)         | **Two tasks**, `ktos_SendMsg(MSG_SET_MODE)` from UART to LED   |
| [`button_control/`](button_control/README.md)   | **Two tasks**, debouncer publishes `MSG_BUTTON_EVENT` to UI    |
| [`adc/`](adc/README.md)                         | Single task, 1 Hz timer wake, A0 = ADC channel **7**          |
| [`i2c/`](i2c/README.md)                         | Single task, 5 s timer wake doing TWI bus scan                 |
| [`spi/`](spi/README.md)                         | Single task, MOSI→MISO loopback on the ICSP header            |

---

## Board Overview

| Item              | Value                                                       |
|-------------------|-------------------------------------------------------------|
| Board             | Arduino Leonardo                                            |
| MCU               | ATmega32U4 (AVR 8-bit)                                      |
| Clock             | 16 MHz                                                      |
| Logic level       | 5 V                                                         |
| Flash             | 32 KB                                                       |
| SRAM              | 2.5 KB                                                      |
| EEPROM            | 1 KB                                                        |
| KTOS BSP          | `bsp/atmega32u4/`                                           |
| KTOS tick source  | Timer1 CTC, 1 ms                                            |
| Toolchain         | avr-gcc + avr-libc (no Arduino framework)                   |
| Upload protocol   | `avr109` (CDC bootloader)                                   |
| Serial output     | USART1 on D0/D1 via external USB-to-serial adapter          |

---

## ATmega32U4 → Leonardo Pin Map

Several pin assignments differ significantly from the UNO/Nano.

| Leonardo label | AVR pin | Used by                      | Notes                              |
|----------------|---------|------------------------------|------------------------------------|
| `D0 / RX`      | `PD2`   | USART1 RX                    | Connect adapter TX here            |
| `D1 / TX`      | `PD3`   | USART1 TX                    | Connect adapter RX here            |
| `D2 / SDA`     | `PD1`   | I²C SDA (`i2c`)              | Shared with TWI — avoid as GPIO    |
| `D3 / SCL`     | `PD0`   | I²C SCL (`i2c`)              | Shared with TWI — avoid as GPIO    |
| `D4`           | `PD4`   | Button (`button_control`)    | Safe GPIO — no peripheral conflict |
| `D13`          | `PC7`   | LED (`led_control`)          | Not a SPI pin — LED won't flicker  |
| `A0`           | `PF7`   | ADC channel **7** (`adc`)    | ATmega32U4: A0=PF7=ADC7, not ADC0  |
| ICSP-1         | `PB3`   | SPI MISO (`spi`)             | ICSP header only                   |
| ICSP-3         | `PB1`   | SPI SCK  (`spi`)             | ICSP header only                   |
| ICSP-4         | `PB2`   | SPI MOSI (`spi`)             | ICSP header only                   |
| —              | `PB0`   | SPI SS   (`spi`)             | Not on header; driven in software  |

> **Key differences from UNO/Nano:**
> - `D13 = PC7` (not PB5) — LED does **not** flicker during SPI
> - `D2 = PD1 = SDA`, `D3 = PD0 = SCL` — do not use D2/D3 as plain GPIO
> - `A0 = PF7 = ADC channel 7` — set `ADMUX` MUX bits to `0b00111`
> - SPI is on the ICSP header — not on D10-D13 as on the UNO

---

## Common Setup

### 1. Install PlatformIO

```bash
python3 -m pip install --user platformio
pio --version
```

Or install the **PlatformIO IDE** extension in VS Code.

### 2. Open an example

```bash
cd examples/Arduino/Leonardo/adc     # or any other example folder
```

### 3. Build and upload

```bash
pio run                 # compile
pio run -t upload       # flash via CDC bootloader (press reset if needed)
```

> The Leonardo bootloader requires the host to open the serial port at
> **1200 baud** to trigger the DFU reset sequence.  PlatformIO does
> this automatically with `upload_protocol = avr109`.

### 4. Open serial monitor (via adapter)

```bash
pio device monitor -b 115200 --port /dev/ttyUSB0   # Linux
pio device monitor -b 115200 --port COM3            # Windows
```

---

## What gets compiled

```ini
[env:leonardo]
platform        = atmelavr
board           = leonardo
upload_protocol = avr109
monitor_speed   = 115200
build_src_filter =
    +<*>
    +<../../../../../core/ktos.c>
    +<../../../../../bsp/atmega32u4/ktos_bsp.c>
build_flags =
    -Os -Wall -Wextra
    -I../../../../core
    -I../../../../bsp/atmega32u4
```

- `core/ktos.c` — the platform-independent KTOS scheduler.
- `bsp/atmega32u4/ktos_bsp.c` — Timer1 init, context switch, 2-byte PC stack frame.

---

## Troubleshooting

| Symptom                                              | Likely cause                      | Fix                                                                       |
|------------------------------------------------------|-----------------------------------|---------------------------------------------------------------------------|
| `avrdude: butterfly_recv(): programmer not responding`| Bootloader not entered            | Press reset once, then run `pio run -t upload` within 8 seconds           |
| No output in serial monitor                          | Adapter not connected / wrong port| Check adapter wiring: adapter-TX → D0, adapter-RX → D1                   |
| Garbled output                                       | Wrong baud rate                   | Set terminal to **115200 8N1**                                            |
| ADC reads wrong voltage on A0                        | Wrong ADC channel                 | A0=PF7=ADC channel **7** on 32U4 — ADMUX must be `(REFS0<<6) | 7`        |
| SPI: `Loopback FAIL`                                 | Wrong jumper location             | Jumper goes ICSP-4 (MOSI) → ICSP-1 (MISO), not on the digital header     |
| I²C: `No devices found`                              | Missing pull-ups                  | Add 4.7 kΩ from D2 (SDA) and D3 (SCL) to 5 V                             |
| Button: no events                                    | Wrong pin                         | Button example uses **D4 (PD4)**, not D2 (which is SDA on the Leonardo)   |
| `[KTOS FATAL]` on boot                               | Heap exhausted                    | Reduce `StackSize` in `ktos_InitTask()` calls                             |
