### KTOS on STM32F103 Blue Pill — Examples

KTOS bare-metal examples for the **STM32F103C8T6 Blue Pill** (ARM Cortex-M3 @ 72 MHz).
All peripherals accessed directly through STM32F103 registers — no HAL, no CMSIS, no Arduino framework.

---

## Examples

| Folder              | Demonstrates                                                              |
|---------------------|---------------------------------------------------------------------------|
| [`uart/`](uart/)    | **Two tasks**, RX line assembler → PING/INFO/HELP command dispatcher.    |
| [`led_control/`](led_control/) | **Two tasks**, `ktos_SendMsg(MSG_SET_MODE)` from UART task to LED task (PC13). |
| [`button_control/`](button_control/) | **Two tasks**, 30 ms debouncer on PB0 → `MSG_BUTTON_EVENT` → LED toggle on PC13. |
| [`adc/`](adc/)      | One task, 1 Hz read of PA0 (ADC1 CH0), 12-bit, 3.3 V ref.              |
| [`i2c/`](i2c/)      | One task, I2C1 scanner (PB6=SCL, PB7=SDA), 100 kHz, scan every 5 s.   |
| [`spi/`](spi/)      | One task, SPI1 external loopback (PA5/PA6/PA7), 9 MHz. *(PA6–PA7 jumper required.)* |

---

## Board Overview

| Item              | Value                                                        |
|-------------------|--------------------------------------------------------------|
| Board             | STM32F103C8T6 "Blue Pill"                                    |
| MCU               | STM32F103C8T6 (ARM Cortex-M3)                               |
| Clock             | 72 MHz (HSE 8 MHz → PLL ×9)                                 |
| Logic level       | **3.3 V** (not 5 V tolerant on most pins)                   |
| Flash             | 64 KB at 0x08000000                                          |
| SRAM              | 20 KB at 0x20000000                                          |
| KTOS BSP          | [`bsp/stm32f103/`](../../bsp/stm32f103/)                     |
| KTOS tick source  | **SysTick**, 1 ms (LOAD = 71999)                             |
| Programmer        | ST-Link V2 (SWD)                                             |
| PlatformIO board  | `bluepill_f103c8`                                            |

---

## Pin Map

| Signal          | Pin   | CRL/CRH config        | Used by                                    |
|-----------------|-------|-----------------------|--------------------------------------------|
| USART1 TX       | PA9   | AF PP 50 MHz (0xB)    | uart, led_control, button_control, adc, spi |
| USART1 RX       | PA10  | Input floating (0x4)  | uart                                       |
| LED (active low)| PC13  | Out PP 2 MHz (0x2)    | led_control, button_control                |
| Button          | PB0   | Input pull-up (0x8)   | button_control                             |
| ADC1 CH0        | PA0   | Analog input (0x0)    | adc                                        |
| I2C1 SCL        | PB6   | AF OD 50 MHz (0xF)    | i2c                                        |
| I2C1 SDA        | PB7   | AF OD 50 MHz (0xF)    | i2c                                        |
| SPI1 SCK        | PA5   | AF PP 50 MHz (0xB)    | spi                                        |
| SPI1 MISO       | PA6   | Input floating (0x4)  | spi                                        |
| SPI1 MOSI       | PA7   | AF PP 50 MHz (0xB)    | spi                                        |

---

## Clock Configuration

Set by `bsp/stm32f103/startup.c` before `main()`:

| Domain  | Source           | Frequency |
|---------|------------------|-----------|
| SYSCLK  | PLL (HSE ×9)     | 72 MHz    |
| AHB     | SYSCLK / 1       | 72 MHz    |
| APB2    | AHB / 1          | 72 MHz    |
| APB1    | AHB / 2          | 36 MHz    |
| ADC     | APB2 / 6         | 12 MHz    |

---

## Required Hardware

| Item                     | Quantity | Notes                                               |
|--------------------------|----------|-----------------------------------------------------|
| Blue Pill (STM32F103C8T6)| 1        | 8 MHz HSE crystal must be populated                 |
| ST-Link V2               | 1        | Connect SWDIO/SWDCLK/GND/3.3V to the SWD header    |
| 3.3 V USB-serial adapter | 1        | UART output: connect adapter RX → PA9               |
| Tactile button           | 0 or 1   | `button_control`: wire PB0 to GND                  |
| Signal source            | 0 or 1   | `adc`: 0–3.3 V signal to PA0                       |
| I²C breakout             | 0+       | `i2c`: PB6 (SCL) / PB7 (SDA) with 4.7 kΩ pull-ups |
| Jumper wire              | 0 or 1   | `spi`: bridge PA6 (MISO) ↔ PA7 (MOSI)              |

> **3.3 V logic only.** Most STM32F103 GPIO pins are **not** 5 V tolerant
> (except certain pins marked FT in the datasheet — PA9/PA10 are FT).

---

## Build and Flash

### 1. Install PlatformIO

```bash
python3 -m pip install --user platformio
```

### 2. Build and flash

```bash
git clone https://github.com/baamiis/KTOS.git
cd KTOS/examples/stm32f103/uart   # or any example
pio run                            # compile
pio run -t upload                  # flash via ST-Link
pio device monitor -b 115200       # open serial console
```

### 3. ST-Link wiring (SWD)

| ST-Link pin | Blue Pill SWD header |
|-------------|----------------------|
| SWDIO       | IO                   |
| SWDCLK      | CLK                  |
| GND         | GND                  |
| 3.3V        | 3.3V (power or leave board-powered) |

---

## Why no `framework = arduino`?

These examples are fully bare-metal. `bsp/stm32f103/` provides everything:

- `stm32f103.ld` — linker script: 64 KB flash at 0x08000000, 20 KB RAM.
- `startup.c` — vector table, `Reset_Handler`, 72 MHz PLL clock init (HSE 8 MHz → ×9), `.data` copy, `.bss` zero.
- `ktos_bsp.c` — SysTick 1 ms tick, Cortex-M3 context switch (`push {r4-r11}`).

The Cortex-M3 context switch is simpler than Cortex-M0+: `push/pop {r4-r11}` works directly without the indirect-via-low-register workaround.

---

## Troubleshooting

| Symptom                          | Likely cause                                 | Fix                                                      |
|----------------------------------|----------------------------------------------|----------------------------------------------------------|
| Upload fails, "No target found"  | ST-Link not connected or wrong pinout        | Check SWD wiring: SWDIO/SWDCLK/GND/3.3V                |
| Upload fails, "device not found" | Blue Pill has no HSE crystal populated       | Verify 8 MHz crystal is soldered (some bare boards skip it) |
| No serial output                 | Wrong baud or wrong TX pin                   | 115200 baud, connect adapter RX to PA9                  |
| `spi` FAIL on every byte         | PA6–PA7 jumper missing                       | Bridge PA6 (MISO) to PA7 (MOSI) with a wire             |
| `i2c` hangs on first scan        | Missing pull-up resistors                    | Add 4.7 kΩ from PB6/PB7 to 3.3 V                       |
| `adc` reads 0 or 4095 always     | Floating input                               | Connect a 0–3.3 V signal or pot to PA0                  |
| SysTick fires but tasks stall    | KTOS Emergency called; check USART1 output   | Confirm `uart_putc` not blocking before USART1 is inited|
