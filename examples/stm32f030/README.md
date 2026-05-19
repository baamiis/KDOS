### KTOS on STM32F030 — Examples

KTOS bare-metal examples for the **STM32F030R8 Nucleo** (ARM Cortex-M0 @ 48 MHz).
All peripherals accessed directly through registers — no HAL, no CMSIS.

---

## Examples

| Folder              | Demonstrates                                                              |
|---------------------|---------------------------------------------------------------------------|
| [`uart/`](uart/)    | **Two tasks**, RX line assembler → PING/INFO/HELP command dispatcher.    |
| [`led_control/`](led_control/) | **Two tasks**, `ktos_SendMsg(MSG_SET_MODE)` from UART task to LED task (LD2/PA5). |
| [`button_control/`](button_control/) | **Two tasks**, 30 ms debouncer on B1 (PC13) → `MSG_BUTTON_EVENT` → LD2 toggle. |
| [`adc/`](adc/)      | One task, 1 Hz read of PA0 (ADC1 CH0), 12-bit, 3.3 V ref.              |
| [`i2c/`](i2c/)      | One task, I2C1 scanner (PB6=SCL, PB7=SDA), 100 kHz, scan every 5 s.   |
| [`spi/`](spi/)      | One task, SPI1 external loopback (PA5/PA6/PA7), 6 MHz. *(PA6–PA7 jumper required.)* |

---

## Board Overview

| Item              | Value                                                        |
|-------------------|--------------------------------------------------------------|
| Board             | STM32F030R8 Nucleo-64                                        |
| MCU               | STM32F030R8T6 (ARM Cortex-M0)                               |
| Clock             | 48 MHz (HSI 8 MHz → HSI/2 → PLL ×12)                        |
| Logic level       | **3.3 V**                                                    |
| Flash             | 64 KB at 0x08000000                                          |
| SRAM              | 8 KB at 0x20000000                                           |
| KTOS BSP          | [`bsp/stm32f030/`](../../bsp/stm32f030/)                     |
| KTOS tick source  | **SysTick**, 1 ms (LOAD = 47999)                             |
| Programmer        | ST-Link/V2-1 (on-board)                                      |
| PlatformIO board  | `nucleo_f030r8`                                              |

---

## Key Differences from STM32F103

| Feature       | STM32F103 (F1)                   | STM32F030 (F0)                         |
|---------------|----------------------------------|----------------------------------------|
| GPIO base     | 0x4001xxxx                       | **0x4800xxxx**                         |
| GPIO config   | CRL/CRH (4 bits/pin)             | **MODER/OTYPER/PUPDR/AFR** (2 bits/pin)|
| GPIO clocks   | RCC_APB2ENR                      | **RCC_AHBENR**                         |
| USART regs    | SR / DR                          | **ISR / TDR / RDR**                    |
| ADC regs      | CR1/CR2/SQR3/DR                  | **CR / CFGR1 / CHSELR / DR**           |
| I2C regs      | CCR / TRISE / CR                 | **TIMINGR / CR2 with AUTOEND**         |
| Context switch | `push {r4-r11}` direct          | Indirect via R4–R7 for R8–R11          |

---

## Pin Map

| Signal            | Pin   | Config                   | Used by                                    |
|-------------------|-------|--------------------------|--------------------------------------------|
| USART2 TX (ST-Link)| PA2  | AF1, MODER=10            | all examples                               |
| USART2 RX (ST-Link)| PA3  | AF1, MODER=10            | uart                                       |
| LD2 LED (active high)| PA5| output, MODER=01         | led_control, button_control                |
| ADC1 CH0          | PA0   | analog, MODER=11          | adc                                        |
| SPI1 SCK          | PA5   | AF0, MODER=10             | spi *(same pin as LD2 — LED inactive)*     |
| SPI1 MISO         | PA6   | AF0, MODER=10             | spi                                        |
| SPI1 MOSI         | PA7   | AF0, MODER=10             | spi                                        |
| B1 USER button    | PC13  | input pull-up, MODER=00  | button_control                             |
| I2C1 SCL          | PB6   | AF1, open-drain           | i2c                                        |
| I2C1 SDA          | PB7   | AF1, open-drain           | i2c                                        |

---

## Build and Flash

```bash
git clone https://github.com/baamiis/KTOS.git
cd KTOS/examples/stm32f030/uart   # or any example
pio run                            # compile
pio run -t upload                  # flash via on-board ST-Link
pio device monitor -b 115200       # open ST-Link virtual COM
```

No external hardware required for `uart`, `led_control`, `button_control` — everything is on the Nucleo board.

---

## Required Hardware

| Item                     | Quantity | Notes                                               |
|--------------------------|----------|-----------------------------------------------------|
| Nucleo-F030R8            | 1        | On-board ST-Link, LED, button, USB virtual COM      |
| Micro-USB cable          | 1        | Connects to the CN1 USB connector                   |
| Signal source            | 0 or 1   | `adc`: 0–3.3 V to PA0 (CN7 pin 28 / A0)           |
| I²C breakout             | 0+       | `i2c`: PB6 (SCL) / PB7 (SDA) with 4.7 kΩ pull-ups |
| Jumper wire              | 0 or 1   | `spi`: bridge PA6 (MISO) ↔ PA7 (MOSI)              |

---

## Troubleshooting

| Symptom                      | Likely cause                              | Fix                                               |
|------------------------------|-------------------------------------------|---------------------------------------------------|
| No serial output             | Wrong COM port or baud                    | Use ST-Link virtual COM, 115200 baud              |
| `spi` FAIL on every byte     | PA6–PA7 jumper missing                    | Bridge PA6 (MISO) to PA7 (MOSI)                  |
| `i2c` hangs on first scan    | Missing pull-up resistors                 | Add 4.7 kΩ from PB6/PB7 to 3.3 V                |
| Upload fails "No target"     | USB cable or ST-Link issue                | Replug USB; check CN1 connector (not CN5)         |
| Tasks run but LED wrong      | PA5 shares SCK and LED                   | Normal: in `spi` example LED stays dark           |
