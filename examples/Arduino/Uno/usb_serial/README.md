# KTOS - Arduino Uno USB-Serial CLI Example (Bare-Metal)

An interactive shell over the Uno's **USB CDC** link.  Two KTOS tasks
take turns: one drains the USB-serial input, the other parses commands
and drives the LED, the ADC, the button, or echoes text back.

KTOS is the operating system - no Arduino framework, no `Serial`, no
`String`.  Direct register access for USART0, ADC, `PORTB` and `PIND`.

---

## Overview

```
   PC terminal ──► USB ──► ATmega16U2 (CDC) ──► USART0 ──► rx_task
                                                              ('R')
                                                              │
                                                              │ ktos_SendMsg(MSG_LINE_READY)
                                                              ▼
                                                       ┌──────────────┐
                                                       │   cmd_task   │
                                                       │    ('C')     │
                                                       └──────┬───────┘
                                                              │
                            ┌─────────────┬──────────────┬────┴────┐
                            ▼             ▼              ▼         ▼
                          LED            ADC            BTN       USART0 reply
                         (D13/PB5)      (A0/PC0)      (D2/PD2)
```

- **`rx_task`** wakes every 10 ms, drains USART0's RX FIFO, echoes
  every byte (so the user sees what they typed), and assembles
  characters into a 64-byte line buffer.  On `\r` or `\n` it posts
  `MSG_LINE_READY` to `cmd_task`.
- **`cmd_task`** sleeps with `KTOS_MSG_SLEEP_INDEFINITLY` - **zero CPU between events**.
  On `MSG_LINE_READY` it parses the first whitespace-delimited token
  and dispatches to one of the handlers.

---

## What the Uno adds on top of the Nano `uart` example

The Nano [`uart`](../../Nano/uart/README.md) example demonstrates the
same two-task line-assembler pattern with `PING` / `INFO` / `HELP`.
This example takes the next step:

- **The USB link is a control plane.**  A single USB-CDC connection
  gives the host control over GPIO output (LED), digital input
  (button), and the ADC - everything you'd want to do over a debug
  console.
- **Adding a new command is one `else if`** inside `cmd_task` - no
  new task, no new resources, no scheduler changes.

If you understand the Nano `uart` example, this one will read in
about 30 seconds.

---

## Commands

| Command                       | Effect                                              |
|-------------------------------|-----------------------------------------------------|
| `HELP`                        | List commands                                       |
| `INFO`                        | Board info, KTOS info, current LED + button state  |
| `LED ON` / `LED OFF` / `LED TOGGLE` | Drive D13                                     |
| `ADC`                         | One A0 sample (raw + millivolts)                    |
| `BTN`                         | One D2 read (active-low with internal pull-up)      |
| `ECHO <text>`                 | Print the rest of the line back                     |

Commands are **case-insensitive**.  Multi-word arguments to `ECHO`
preserve their original case and spacing.

---

## Hardware Required

| Item                 | Quantity | Notes                                            |
|----------------------|----------|--------------------------------------------------|
| Arduino Uno R3       | 1        | Or any ATmega328P board with USB-serial         |
| USB-A to USB-B cable | 1        | Genuine Uno uses USB-B; many clones use USB-C/mini |
| Push-button (optional) | 1      | For the `BTN` command — wire between D2 and GND |
| 10 kΩ potentiometer (optional) | 1 | For the `ADC` command — wiper to A0          |

The on-board LED on D13 (= `PB5`) is sufficient for the `LED` command.
The other peripherals are optional - `BTN` returns "released" if D2 is
floating high (pull-up active), and `ADC` reads whatever is on A0.

---

## Wiring

None required for the LED.  Optional:

| Optional peripheral | Nano label | AVR pin |
|---------------------|------------|---------|
| Button to GND       | `D2`       | `PD2`   |
| Potentiometer wiper | `A0`       | `PC0`   |

(Button uses the internal pull-up - no external resistor.)

---

## How It Works

1. `main()` calls `uart_init()`, `led_init()`, `btn_init()`,
   `adc_init()` then creates two KTOS tasks.

2. `cmd_task` is registered first so `rx_task` has a valid handle:

   ```c
   g_cmd_task = ktos_InitTask(cmd_task, 96, 4, 'C');
                ktos_InitTask(rx_task,  64, 2, 'R');
   ktos_RunOS();
   ```

3. On `KTOS_MSG_TYPE_INIT`:
   - `cmd_task` prints the banner and returns `KTOS_MSG_SLEEP_INDEFINITLY`.
   - `rx_task` clears its line-assembly state and returns `10`.

4. Every 10 ms `rx_task` wakes with `KTOS_MSG_TYPE_TIMER`:
   - Loop until `UCSR0A & (1<<RXC0)` is clear.
   - Echo every byte.
   - Append non-newline bytes into `g_line_buf`.
   - On a newline, terminate the buffer and call
     `ktos_SendMsg(g_cmd_task, MSG_LINE_READY, ...)`.

5. KTOS marks `cmd_task` ready and dispatches it on the next
   scheduling pass.  It tokenises the line, dispatches to the right
   handler, writes the response, and returns `KTOS_MSG_SLEEP_INDEFINITLY`.

The receive buffer is fixed-size and never allocated.  Lines longer
than 63 characters are silently truncated - the buffer cannot overrun.

---

## Download, Build, and Flash

### 1. Prerequisites

- **PlatformIO** - install the [VS Code extension](https://platformio.org/install/ide?install=vscode), or the CLI:
  ```bash
  python3 -m pip install --user platformio
  pio --version
  ```
- **USB-serial driver** - genuine Unos with the ATmega16U2 work out of
  the box on every modern OS.  Clones with the **CH340** chip need the
  [WCH CH340 driver](https://www.wch.cn/downloads/CH341SER_EXE.html)
  on Windows / macOS.

### 2. Get the source

```bash
git clone https://github.com/baamiis/KTOS.git
cd KTOS/examples/Arduino/Uno/usb_serial
```

(Or download the repository as a ZIP from GitHub and `cd` into the
same folder.)

### 3. Connect the Uno and find its port

Plug the Uno into your computer with a USB cable, then:

```bash
pio device list
```

Look for an entry whose description contains `USB Serial`, `Arduino Uno`,
or `CH340`.  Typical port names:

| OS       | Example port                     |
|----------|----------------------------------|
| Linux    | `/dev/ttyACM0` (genuine) or `/dev/ttyUSB0` (clone) |
| macOS    | `/dev/cu.usbmodem14101`          |
| Windows  | `COM4`                           |

### 4. Build

```bash
pio run
```

The first build downloads `toolchain-atmelavr` (~30 MB, one time only).
PlatformIO pulls `core/ktos.c` and `bsp/atmega328p/ktos_bsp.c` into the
build automatically.  Output: `.pio/build/uno/firmware.elf` (and `.hex`).

### 5. Flash

```bash
pio run -t upload
```

PlatformIO talks to the Uno's optiboot bootloader via `avrdude -c arduino`
at 115 200 baud - no buttons to press.  If auto-detection fails:

```bash
pio run -t upload --upload-port COM4          # Windows
pio run -t upload --upload-port /dev/ttyACM0  # Linux / macOS
```

### 6. Open the serial monitor

```bash
pio device monitor -b 115200
```

Press the Uno's **reset** button if no banner appears.

> **Only one process can hold the USB port at a time.**  Close the
> monitor before re-running `pio run -t upload`.

---

## Expected Output

Boot:

```
=============================
  KTOS USB-Serial CLI
=============================
Type HELP for commands.
```

A short session:

```
HELP
Commands:
  HELP                       this list
  INFO                       board + KTOS info
  LED ON | OFF | TOGGLE      drive D13
  ADC                        read A0 once
  BTN                        read D2 once
  ECHO <text>                echo the rest of the line
LED ON
LED ON
ADC
A0 raw=523 mv=2556
BTN
Button: released
ECHO hello world
hello world
INFO
Board   : Arduino Uno R3
MCU     : ATmega328P @ 16 MHz
OS      : KTOS (cooperative, Timer1 1 ms tick)
SRAM    : 2 KB     Flash: 32 KB     EEPROM: 1 KB
Bridge  : ATmega16U2 USB CDC <-> USART0
LED D13 : ON
BTN D2  : released
```

---

## Troubleshooting

| Symptom                              | Likely cause                                      | Fix                                                       |
|--------------------------------------|---------------------------------------------------|-----------------------------------------------------------|
| No banner after upload               | Monitor not yet open / wrong baud                 | Open the monitor at **115 200**, reset the Uno            |
| Each character echoed twice          | Monitor has local echo enabled                    | Disable local echo in your terminal                       |
| Commands not recognised              | No newline at end of line                         | Set monitor line ending to "Newline" or "CRLF"            |
| `LED` command silently ignored       | Missing argument                                  | Use `LED ON` / `LED OFF` / `LED TOGGLE`                   |
| `pio run -t upload` times out        | Old bootloader on a clone                         | Add `board_upload.speed = 57600` to `platformio.ini`      |
| Banner prints but commands never reply | Timer1 ISR not hooked up                        | Confirm `ISR(TIMER1_COMPA_vect)` is present in `main.c`   |

---

## Notes for KDOS Integration

- `uart_putc()` / `uart_puts()` / `uart_get_byte()` map to future
  `ktos_hal_uart_*` calls.
- `led_*` / `btn_*` map to `ktos_hal_gpio_*`.
- `adc_read()` maps to `ktos_hal_adc_read()`.
- The whole shell is a single tokeniser + a `strcmp` chain inside
  `cmd_task` - adding new commands is trivial without rearchitecting
  anything.
