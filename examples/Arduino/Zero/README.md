### KTOS on Arduino Zero — Examples

KTOS bare-metal examples for the **Arduino Zero** (Atmel ATSAMD21G18, ARM
Cortex-M0+ @ 48 MHz).  No Arduino framework — all peripherals (SERCOM,
PORT, ADC, TC) are accessed directly through SAMD21 registers.

---

## Examples

| Folder                                           | Demonstrates                                                             |
|--------------------------------------------------|--------------------------------------------------------------------------|
| [`uart/`](uart/)                                 | **Two tasks**, RX line assembler → PING/INFO/HELP command dispatcher.   |
| [`led_control/`](led_control/)                   | **Two tasks**, `ktos_SendMsg(MSG_SET_MODE)` from UART to LED (D13/PA17). |
| [`button_control/`](button_control/)             | **Two tasks**, 30 ms debouncer on D2 (PA14) → `MSG_BUTTON_EVENT`.       |
| [`adc/`](adc/)                                   | One task, 1 Hz read of A0 (PA02/AIN0), 12-bit, ref=VCC/2.              |
| [`i2c/`](i2c/)                                   | One task, SERCOM3 I2C scanner (D20=SDA/PA22, D21=SCL/PA23), 100 kHz.   |
| [`spi/`](spi/)                                   | One task, SERCOM1 SPI hardware loopback (LOOPBACK bit), 1 MHz.          |

---

## Board Overview

| Item              | Value                                                        |
|-------------------|--------------------------------------------------------------|
| Board             | Arduino Zero                                                 |
| MCU               | Atmel ATSAMD21G18 (ARM Cortex-M0+)                          |
| Clock             | 48 MHz (DFLL48M locked to XOSC32K)                          |
| Logic level       | **3.3 V** (not 5 V tolerant)                                 |
| Flash             | 256 KB (248 KB usable — 8 KB for bootloader)                 |
| SRAM              | 32 KB                                                        |
| KTOS BSP          | [`bsp/samd21g18/`](../../../bsp/samd21g18/)                  |
| KTOS tick source  | **TC3**, 1 ms (GCLK0 48 MHz, MFRQ, CC0=47999)               |
| USB connectors    | 2 — Programming Port (EDBG, near RESET) and Native USB       |
| Bootloader        | BOSSA / UF2 via the Programming Port                         |
| PlatformIO board  | `zero`                                                       |

---

## SAMD21G18 → Arduino Zero Pin Map

| Arduino label | SAMD21G18 pin | Peripheral                       | Used by                        |
|---------------|---------------|----------------------------------|--------------------------------|
| `D0` (RX)     | `PA11`        | SERCOM0 PAD[3]                   | `i2c` serial output (RX)       |
| `D1` (TX)     | `PA10`        | SERCOM0 PAD[2]                   | `i2c` serial output (TX)       |
| `D2`          | `PA14`        | GPIO input + pull-up             | `button_control`               |
| `D11`         | `PA16`        | SERCOM1 PAD[0] = MOSI            | `spi`                          |
| `D12`         | `PA19`        | SERCOM1 PAD[3] = MISO            | `spi` (internal loopback)      |
| `D13` (LED)   | `PA17`        | GPIO output / SERCOM1 PAD[1] SCK | `led_control`, `spi`           |
| `A0`          | `PA02`        | AIN0                             | `adc`                          |
| `SDA` (D20)   | `PA22`        | SERCOM3 PAD[0] or SERCOM5 PAD[0] | `i2c` (SERCOM3 C), others UART |
| `SCL` (D21)   | `PA23`        | SERCOM3 PAD[1] or SERCOM5 PAD[1] | `i2c` (SERCOM3 C), others UART |

### SERCOM assignment

| SERCOM   | Function         | Pins           | Used by                             |
|----------|------------------|----------------|-------------------------------------|
| SERCOM5  | USART 115200     | PA22(TX)/PA23(RX) | `uart`, `led_control`, `button_control`, `adc`, `spi` |
| SERCOM0  | USART 115200     | PA10(TX)/PA11(RX) | `i2c` (separate from I2C pins)   |
| SERCOM1  | SPI master 1 MHz | PA16/PA17/PA19  | `spi`                               |
| SERCOM3  | I2C master 100 kHz | PA22/PA23    | `i2c`                               |

> **PA22/PA23 mux note**: SERCOM5 uses these pins via peripheral function D;
> SERCOM3 uses them via peripheral function C.  They cannot be active
> simultaneously, but since each example is a separate binary this is fine.

---

## Required Hardware

| Item                 | Quantity | Notes                                              |
|----------------------|----------|----------------------------------------------------|
| Arduino Zero         | 1        | —                                                  |
| Micro-USB cable      | 1        | Plug into the **Programming Port** (near RESET).   |
| Tactile push-button  | 0 or 1   | `button_control`: wire between D2 and GND.         |
| Signal source        | 0 or 1   | `adc`: 0–1.65 V signal (or pot between 3.3 V & GND with divider) to A0. |
| I²C breakout         | 0+       | `i2c`: connect to D20 (SDA) / D21 (SCL).          |
| 3.3 V USB-serial     | 0 or 1   | `i2c` only: connect to D1 (TX) / D0 (RX) for output. |

> **3.3 V logic only.** The SAMD21 GPIO is not 5 V tolerant.

---

## Common Setup

### 1. Install PlatformIO

```bash
python3 -m pip install --user platformio
pio --version
```

### 2. Build and flash

```bash
git clone https://github.com/baamiis/KTOS.git
cd KTOS/examples/Arduino/Zero/uart   # or any example
pio run                              # compile
pio run -t upload                    # flash via Programming Port
pio device monitor -b 115200         # open serial console
```

### 3. Use the correct USB port

The Zero has two USB connectors:

| Connector            | Position              | Purpose                                     |
|----------------------|-----------------------|---------------------------------------------|
| **Programming Port** | Near the RESET button | EDBG: flash via BOSSA + SERCOM5 serial      |
| Native USB           | Other connector       | SAM21 native USB (CDC, HID, MSC)            |

Plug into the **Programming Port** for all these examples.

---

## Why no `framework = arduino`?

These examples are fully bare-metal.  `bsp/samd21g18/` provides everything:

- `samd21g18.ld` — linker script: application at 0x00002000 (after 8 KB bootloader).
- `startup.c` — vector table, `Reset_Handler`, 48 MHz clock init via DFLL48M
  locked to XOSC32K (32.768 kHz crystal), VTOR relocation.
- `ktos_bsp.c` — TC3 init for 1 ms KTOS tick, Cortex-M0+ context switch.

The Cortex-M0+ context switch differs from M3: high registers R8-R11 must
be moved to R4-R7 before they can be pushed or popped.

---

## Troubleshooting

| Symptom                                              | Likely cause                                  | Fix                                              |
|------------------------------------------------------|-----------------------------------------------|--------------------------------------------------|
| Board not found by `pio device list`                 | Wrong USB port plugged in                     | Use Programming Port (near RESET button)         |
| Upload fails with "no device found"                  | Zero not in bootloader mode                   | Double-tap RESET to force BOSSA bootloader       |
| No serial output                                     | Wrong baud or wrong port                      | Set monitor to **115 200**, Programming Port     |
| `i2c` example: no output at all                      | Serial goes to D1/D0, not EDBG               | Connect a 3.3 V USB-serial adapter to D1 (TX)   |
| 3.3 V peripheral damaged                            | Applied 5 V to GPIO                           | SAMD21 is **not** 5 V tolerant — use a level shifter |
| TC3 tick fires but tasks don't advance               | INTFLAG.OVF not cleared in TC3_Handler       | Confirm `*(volatile uint8_t*)0x42002C0EUL = 1;` |
