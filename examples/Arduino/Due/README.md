### KTOS on Arduino Due — Examples

KTOS bare-metal examples for the **Arduino Due** (Atmel SAM3X8E, ARM
Cortex-M3 @ 84 MHz).  KTOS replaces the Arduino OS — the application
code never calls any Arduino API; all peripherals (UART, ADC, TWI,
PIO) are accessed through direct SAM3X registers.  The Arduino
framework is pulled in **only** for the boot scaffolding (vector
table, PLL clock setup, linker script) that bare-metal Cortex-M
firmware needs.

---

## Examples

| Folder                                          | Demonstrates                                                       |
|-------------------------------------------------|--------------------------------------------------------------------|
| [`adc/`](adc/)                                  | Single KTOS task, 1 Hz timer wake; reads 12-bit ADC channel 7 (A0). |
| [`i2c/`](i2c/)                                  | Single KTOS task, 5 s rescan of TWI1 (Arduino "Wire" on pins 20/21). |
| [`led_control/`](led_control/)                  | **Two tasks**, `ktos_SendMsg(MSG_SET_MODE)` from UART to LED (D13/PB27). |
| [`button_control/`](button_control/)            | **Two tasks**, debouncer on D2 (PB25) publishes `MSG_BUTTON_EVENT`. |
| [`uart/`](uart/)                                | **Two tasks**, line-assembler hands lines to a command processor.   |

Footprints — all five sit well under 3 % of the Due's 96 KB SRAM and
~3 % of its 512 KB flash:

| Example          | Flash    | RAM     |
|------------------|----------|---------|
| `adc`            | 14 144 B | 2 640 B |
| `led_control`    | 14 912 B | 2 664 B |
| `button_control` | 14 192 B | 2 648 B |
| `uart`           | 14 996 B | 2 712 B |
| `i2c`            | 14 268 B | 2 640 B |

---

## Board Overview

| Item              | Value                                                  |
|-------------------|--------------------------------------------------------|
| Board             | Arduino Due                                            |
| MCU               | Atmel SAM3X8E (ARM Cortex-M3)                          |
| Clock             | 84 MHz                                                 |
| Logic level       | **3.3 V** (NOT 5 V tolerant — different from the AVR boards) |
| Flash             | 512 KB (2 × 256 KB banks)                              |
| SRAM              | 96 KB (64 KB + 32 KB banks)                            |
| KTOS BSP          | [`bsp/sam3x8e/`](../../../bsp/sam3x8e/)                |
| KTOS tick source  | **TC0 channel 0**, 1 ms (TIMER_CLOCK2 = MCK/8, RC = 10 500) |
| USB connectors    | 2 — Programming Port (left, via ATmega16U2) and Native USB (right, SAM3X SOF) |
| Bootloader        | SAM-BA via the Programming Port                        |
| PlatformIO board  | `due`                                                  |

> **The KTOS tick is on TC0, not SysTick.** The Arduino-SAM core
> claims `SysTick_Handler` to drive its own `millis()`.  Moving KTOS to
> a dedicated Timer/Counter peripheral lets the two coexist without
> any modification to the Arduino runtime.

---

## SAM3X8E → Due Pin Map

These examples talk to the chip directly, so this is the table that
matters more than the silkscreened labels:

| Arduino label | AVR-style alias | SAM3X8E pin | Used by                |
|---------------|-----------------|-------------|------------------------|
| `D0` / `RX0`  | —               | `PA8`       | Programming Port UART RX (URXD) — every example |
| `D1` / `TX0`  | —               | `PA9`       | Programming Port UART TX (UTXD) — every example |
| `D2`          | —               | `PB25`      | Button input (`button_control`) |
| `D13`         | —               | `PB27`      | On-board LED (`led_control`) |
| `A0`          | —               | `PA16`      | ADC channel 7 (`adc`) |
| `SDA1` (pin 20) | —             | `PB12`      | TWI1 SDA — "Wire" on Due (`i2c`) |
| `SCL1` (pin 21) | —             | `PB13`      | TWI1 SCL — "Wire" on Due (`i2c`) |

---

## Required Hardware

| Item                 | Quantity | Notes                                            |
|----------------------|----------|--------------------------------------------------|
| Arduino Due          | 1        | —                                                |
| Micro-USB cable      | 1        | Plug into the **Programming Port** (closer to the DC jack). |
| Tactile push-button  | 0 or 1   | For `button_control` (wire between D2 and GND). |
| 10 kΩ potentiometer  | 0 or 1   | For `adc` (wiper to A0, ends to 3.3 V and GND). |
| I²C breakout         | 0+       | For `i2c` (most modules have built-in pull-ups). |

> **3.3 V logic — be careful.** Unlike the Nano/Uno/Pro Mini, the
> Due's GPIO pins are **not** 5 V tolerant.  Drive them with 3.3 V
> sources only or use a level shifter.

---

## Common Setup

### 1. Install PlatformIO

VS Code extension, or CLI:

```bash
python3 -m pip install --user platformio
pio --version
```

PlatformIO will download the ARM toolchain (`toolchain-gccarmnoneeabi`)
and the Atmel-SAM framework on first build (~50 MB combined).

### 2. Open an example

```bash
git clone https://github.com/baamiis/KTOS.git
cd KTOS/examples/Arduino/Due/adc      # or any other example
pio run                               # compile
pio run -t upload                     # flash via the Programming Port
pio device monitor -b 115200          # open the serial console
```

### 3. Connect the right USB port

The Due has **two** USB connectors:

| Connector             | Position       | What it is                                | Use for                       |
|-----------------------|----------------|--------------------------------------------|-------------------------------|
| **Programming Port**  | Closest to the DC jack | ATmega16U2 USB-serial bridge → SAM3X UART | Flashing AND `pio device monitor` |
| Native USB            | Closer to the reset button | SAM3X native USB (CDC, HID, MSC, …)       | Custom USB devices (advanced) |

These examples target the **Programming Port** for both flashing and
serial output.  If `pio device list` doesn't show your board, you
plugged into the Native USB port — switch cables.

### 4. Find the port

```bash
pio device list
```

| OS       | Typical Programming Port name    |
|----------|----------------------------------|
| Linux    | `/dev/ttyACM0`                   |
| macOS    | `/dev/cu.usbmodem*`              |
| Windows  | `COM4` (or similar)              |

If auto-detection fails:

```bash
pio run -t upload --upload-port /dev/ttyACM0
```

---

## Why `framework = arduino` and not bare-metal?

PlatformIO's `atmelsam` platform does **not** expose a "no framework"
mode for the Due (the build pipeline assumes Arduino, CMSIS-mbed, or
Zephyr).  Trying `framework = cmsis` fails immediately with
*"This board doesn't support cmsis framework!"*.  So we pick the
smallest-footprint option: the Arduino framework — but we use
**none of its APIs**.

What the Arduino framework provides for us:
- Vector table at flash address 0
- Reset handler / `_init` chain
- `SystemInit()` clock tree setup (84 MHz from 12 MHz crystal via PLL)
- Linker script (sections, RAM bank mapping)
- An `extern "C"`-callable `main()` that calls our `setup()` and `loop()`

What we provide instead:
- `setup()` initialises our peripherals, registers KTOS tasks, calls
  `ktos_RunOS()` — which never returns.
- `loop()` is an empty stub the linker insists on.
- All peripheral drivers go straight to the SAM3X registers.

The result: KTOS owns the CPU after `setup()` returns into
`ktos_RunOS()`, and your firmware lives at ~14 KB flash + ~2.7 KB RAM
vs. the 30 KB+ a typical Arduino sketch consumes.

---

## What gets compiled

Each example's `platformio.ini` pulls three things into the build:

```ini
build_src_filter =
    +<*>
    +<../../../../../core/ktos.c>
    +<../../../../../bsp/sam3x8e/ktos_bsp.c>

build_flags =
    -Os
    -Wall
    -Wextra
    -I../../../../core
    -I../../../../bsp/sam3x8e
```

- `core/ktos.c` — the platform-independent KTOS scheduler.
- `bsp/sam3x8e/ktos_bsp.c` — TC0 init, context switch, stack frame
  builder.  Cortex-M3 assembly identical to `bsp/stm32f103/`.
- `core/ktos_common.c` is **deliberately excluded** — it defines its
  own `main()` and stub callbacks that would collide with the
  Arduino-SAM core's `main()` and with each example's own callbacks.

---

## Troubleshooting

| Symptom                                                | Likely cause                                       | Fix                                                                  |
|--------------------------------------------------------|----------------------------------------------------|-----------------------------------------------------------------------|
| Board not listed by `pio device list`                  | Cable plugged into the Native USB port              | Switch to the **Programming Port** (closer to the DC jack)           |
| `Erase pin` / `Atmel SMART` upload errors              | First upload after a corrupt sketch                 | Double-tap the reset button to force the SAM-BA bootloader            |
| Banner prints but tasks never fire                      | `TC0_Handler` not defined in your application       | Confirm `extern "C" void TC0_Handler(void) { ...; ktos_timer_irq_handler(); }` is present |
| `multiple definition of SysTick_Handler`                | Tried to drive KTOS from SysTick on Due             | Don't — the SAM3X8E BSP uses TC0 instead; remove any SysTick override |
| Garbled serial output                                  | Wrong baud rate                                     | Set monitor to **115 200**, 8N1                                       |
| 3.3 V peripheral works, 5 V peripheral seems to "kill" the Due | Damaged GPIO from 5 V signal                | Use a level shifter for any 5 V → Due signal                          |

---

## Notes for KDOS Integration

- `uart_init` / `uart_putc` / `uart_get_byte` map to a future
  `ktos_hal_uart_*` family.
- `adc_init` / `adc_read` map to `ktos_hal_adc_*`.
- `led_init` / `led_on` / `led_off` / `button_init` / `button_raw_high`
  map to `ktos_hal_gpio_*`.
- `twi_init` / `twi_probe` map to `ktos_hal_i2c_*`.
- The Due-specific TC0 tick is already encapsulated in the BSP — once
  a `ktos_hal_timer_*` HAL exists, the application's `TC0_Handler`
  shim becomes a one-liner inside the BSP.
