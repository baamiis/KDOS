### KTOS on Arduino UNO — Bare-Metal Examples

Six examples that run on the **Arduino UNO (ATmega328P)** with **KTOS
as the operating system** — no Arduino framework, no Arduino runtime,
no `setup()` / `loop()`.  Every example provides its own `main()`,
talks to USART0, ADC, TWI, SPI, and GPIO through **direct AVR register
access**, and hands the CPU to `ktos_RunOS()` which never returns.

The UNO and Nano share the same MCU (ATmega328P) and the same KTOS BSP
(`bsp/atmega328p/`).  The only differences are the form factor, the
USB-to-serial chip, and the upload protocol — all handled in
`platformio.ini`.

Where it makes sense, examples are split across **two cooperating
tasks** that communicate exclusively through `ktos_SendMsg()`.

---

## Examples

| Example                                         | KTOS feature exercised                                         |
|-------------------------------------------------|----------------------------------------------------------------|
| [`uart/`](uart/README.md)                       | **Two tasks**, line-assembler hands lines to command processor |
| [`led_control/`](led_control/README.md)         | **Two tasks**, `ktos_SendMsg(MSG_SET_MODE)` from UART to LED   |
| [`button_control/`](button_control/README.md)   | **Two tasks**, debouncer publishes `MSG_BUTTON_EVENT` to UI    |
| [`adc/`](adc/README.md)                         | Single task, 1 Hz timer wake — `return 1000;`                 |
| [`i2c/`](i2c/README.md)                         | Single task, 5 s timer wake doing TWI bus scan                 |
| [`spi/`](spi/README.md)                         | Single task, MOSI→MISO loopback self-test every 1 s            |

---

## Board Overview

| Item              | Value                                                       |
|-------------------|-------------------------------------------------------------|
| Board             | Arduino UNO R3                                              |
| MCU               | ATmega328P (AVR 8-bit)                                      |
| Clock             | 16 MHz                                                      |
| Logic level       | 5 V                                                         |
| Flash             | 32 KB (≈30 KB usable)                                       |
| SRAM              | 2 KB                                                        |
| EEPROM            | 1 KB                                                        |
| KTOS BSP          | `bsp/atmega328p/`                                           |
| KTOS tick source  | Timer1 CTC, 1 ms                                            |
| Toolchain         | avr-gcc + avr-libc (no Arduino framework)                   |
| Upload protocol   | `arduino` (STK500v1, USB bootloader)                        |
| Monitor baud      | 115200, 8N1                                                 |

---

## ATmega328P → UNO Pin Map

| UNO label   | AVR pin  | Used by                      | Register bit                      |
|-------------|----------|------------------------------|-----------------------------------|
| `D0 / RX`   | `PD0`    | UART RX (USART0)             | `UDR0` (auto-controlled)          |
| `D1 / TX`   | `PD1`    | UART TX (USART0)             | `UDR0` (auto-controlled)          |
| `D2`        | `PD2`    | Button (`button_control`)    | `PIND` bit 2 / `PORTD` bit 2      |
| `D10`       | `PB2`    | SPI SS   (`spi`)             | `DDRB` / `PORTB` bit 2            |
| `D11`       | `PB3`    | SPI MOSI (`spi`)             | `DDRB` / `PORTB` bit 3            |
| `D12`       | `PB4`    | SPI MISO (`spi`)             | `DDRB` / `PORTB` bit 4 (input)    |
| `D13`       | `PB5`    | LED (`led_control`) + SCK    | `DDRB` / `PORTB` bit 5            |
| `A0`        | `PC0`    | ADC channel 0 (`adc`)        | `ADMUX` ch 0, read `ADC`          |
| `A4 / SDA`  | `PC4`    | I²C SDA (`i2c`)              | TWI peripheral                    |
| `A5 / SCL`  | `PC5`    | I²C SCL (`i2c`)              | TWI peripheral                    |

> **SPI note:** D13 is both the on-board LED and SPI SCK on the UNO.
> The LED flickers briefly (8 µs at 1 MHz) during each SPI transfer —
> this is normal.  For the loopback test, connect **D11 → D12**.

---

## Common Setup

### 1. Install PlatformIO

**VS Code (recommended):**

1. Install [Visual Studio Code](https://code.visualstudio.com/).
2. Install the **PlatformIO IDE** extension.
3. Reload VS Code when prompted.

**Command line:**

```bash
python3 -m pip install --user platformio
pio --version
```

### 2. Open an example

```bash
cd examples/Arduino/UNO/adc          # or any other example folder
```

### 3. Build and upload

```bash
pio run                 # compile
pio run -t upload       # flash via USB bootloader
```

### 4. Open the serial monitor

```bash
pio device monitor -b 115200
```

---

## What gets compiled

```ini
[env:uno]
platform        = atmelavr
board           = uno
upload_protocol = arduino
monitor_speed   = 115200
build_src_filter =
    +<*>
    +<../../../../../core/ktos.c>
    +<../../../../../bsp/atmega328p/ktos_bsp.c>
build_flags =
    -Os -Wall -Wextra
    -I../../../../core
    -I../../../../bsp/atmega328p
```

- `core/ktos.c` — the platform-independent KTOS scheduler.
- `bsp/atmega328p/ktos_bsp.c` — Timer1 init, context switch, stack frame builder.

No Arduino framework is pulled in.

---

## Troubleshooting

| Symptom                                                | Likely cause                  | Fix                                                                         |
|--------------------------------------------------------|-------------------------------|-----------------------------------------------------------------------------|
| `avrdude: stk500_recv(): programmer is not responding` | Wrong baud / old bootloader   | Add `board_upload.speed = 57600` to `platformio.ini`                        |
| Board not listed by `pio device list`                  | Missing USB driver            | Install CH340 driver (clones) or confirm built-in driver for genuine UNOs   |
| Garbled serial output                                  | Wrong baud rate               | Set monitor to **115200**, 8N1                                              |
| SPI: `Loopback FAIL`                                   | Jumper missing                | Connect D11 (MOSI) to D12 (MISO)                                           |
| LED flickers during SPI                                | D13 = SCK (by design)         | Expected — 8 µs pulse at 1 MHz; use a separate GPIO for LED if unwanted     |
| I²C: no devices found                                  | Missing pull-ups              | Add 4.7 kΩ from SDA (A4) and SCL (A5) to 5 V                               |
| Banner prints but tasks never fire                     | Timer1 ISR not hooked up      | Confirm `ISR(TIMER1_COMPA_vect)` is present in `main.c`                     |
| `[KTOS FATAL]` on boot                                 | Heap exhausted creating tasks | Reduce `StackSize` argument(s) to `ktos_InitTask()`                         |
