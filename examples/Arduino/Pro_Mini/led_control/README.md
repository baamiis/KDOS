# KTOS — Arduino Pro Mini LED Control Example (Bare-Metal)

**Two KTOS tasks** cooperate: one watches the serial port, the other
owns the LED.  They communicate exclusively through `ktos_SendMsg()`.

KTOS is the operating system — there is no Arduino framework, no
`Serial`, no `digitalWrite`.  Both peripherals are driven directly
through ATmega328P registers.

This is the showcase example for the KTOS message-passing model.

---

## Overview

```
   USB Serial ──► uart_task ──► ktos_SendMsg(MSG_SET_MODE, mode)
                  ('U')          │
                                 ▼
                          ┌──────────────┐
                          │   led_task   │
                          │    ('L')     │── PORTB bit 5  (D13)
                          └──────────────┘
```

- **`uart_task`** wakes every 20 ms (`return 20`), drains the USART0
  receive FIFO into a 16-byte buffer, and on each complete line sends
  `MSG_SET_MODE` to `led_task`.
- **`led_task`** owns the LED state.  In `BLINK` mode it returns 500 ms
  to keep toggling; in `ON` / `OFF` it returns `KTOS_MSG_SLEEP_INDEFINITLY` and stays
  idle until the next message arrives.

The two tasks share **no mutable state** beyond the message queue —
because KTOS is cooperative, the message handler is naturally atomic.

---

## What it shows about KTOS

- `ktos_SendMsg(task, msgType, Param1, Param2)` queues a message — the
  receiver picks it up next time the scheduler dispatches it.
- A task can return `KTOS_MSG_SLEEP_INDEFINITLY` to sleep **forever** until a message
  arrives.  No CPU is wasted polling.
- Switching a task between periodic (`return 500`) and event-driven
  (`return KTOS_MSG_SLEEP_INDEFINITLY`) is a single return value — no scheduler config.
- User-defined message types live above `KTOS_MSG_TYPE_SYSTEM_START`.

---

## Hardware Required

| Item                 | Quantity |
|----------------------|----------|
| Arduino Pro Mini | 1        |

The on-board LED on D13 (= AVR pin `PB5`) is sufficient.

---

## Wiring

None.  Optional external LED + 220 Ω resistor on D13 for a brighter
indicator.

---

## How It Works

1. `main()` calls `uart_init()` (USART0 at 115200 baud, 8N1) and
   `led_init()` (`DDRB |= (1<<PORTB5)` — D13 as output).
2. Two tasks are registered:

   ```c
   g_led_task = ktos_InitTask(led_task,  64, 4, 'L');
                ktos_InitTask(uart_task, 80, 4, 'U');
   ktos_RunOS();
   ```

3. KTOS dispatches both tasks once with `KTOS_MSG_TYPE_INIT`.
   `led_task` configures the LED and returns `500`; `uart_task` prints
   the banner and returns `20`.
4. Every 20 ms `uart_task` is rewoken with `KTOS_MSG_TYPE_TIMER`.  It
   drains USART0's RX FIFO; when it sees `\n` or `\r` it parses the
   line and calls:

   ```c
   ktos_SendMsg(g_led_task, MSG_SET_MODE, MODE_ON, 0);
   ```

5. KTOS marks `led_task` ready (even though it was sleeping with
   `KTOS_MSG_SLEEP_INDEFINITLY`) and dispatches it on the next scheduling pass.
   `led_task` updates `PORTB` and the mode flag, then returns the
   appropriate sleep value.

The whole program is event-driven from serial input down to the pin
level — no `_delay_ms`, no busy waiting.

---

## Download, Build, and Flash

### 1. Prerequisites

- **PlatformIO** — install the [VS Code extension](https://platformio.org/install/ide?install=vscode), or the CLI:
  ```bash
  python3 -m pip install --user platformio
  pio --version
  ```
- **USB-serial driver** — most low-cost USB-TTL adapters (and Nano-style clones) use the CH340 chip and need the
  [WCH CH340 driver](https://www.wch.cn/downloads/CH341SER_EXE.html) on Windows / macOS.
  FTDI-based adapters (FT232R/RL) work out of the box on every modern OS.

### 2. Get the source

```bash
git clone https://github.com/baamiis/KTOS.git
cd KTOS/examples/Arduino/Pro_Mini/led_control
```

(Or download the repository as a ZIP from GitHub and `cd` into the same folder.)

### 3. Connect the Pro Mini (via USB-TTL adapter) and find its port

Plug your USB-TTL adapter (FTDI / CH340) into your computer (see the [Pro Mini board guide](../README.md) for wiring), then:

```bash
pio device list
```

Look for an entry whose description contains `USB Serial`, `FT232`, or `CH340`.
Typical port names:

| OS       | Example port                     |
|----------|----------------------------------|
| Linux    | `/dev/ttyUSB0` or `/dev/ttyACM0` |
| macOS    | `/dev/cu.usbserial-1410`         |
| Windows  | `COM4`                           |

PlatformIO auto-detects the port in most cases — note it down only if
upload fails to find it.

### 4. Build

```bash
pio run
```

The first build downloads `toolchain-atmelavr` (~30 MB, one time only).
Subsequent builds finish in a few seconds.  PlatformIO automatically
pulls `core/ktos.c` and `bsp/atmega328p/ktos_bsp.c` into the build via
`build_src_filter`.  Output lands at `.pio/build/nanoatmega328/firmware.elf`
(and `.hex`).

### 5. Flash

```bash
pio run -t upload
```

PlatformIO talks to the Pro Mini's optiboot bootloader via the USB-TTL adapter via `avrdude -c arduino`
(STK500v1) — no buttons to press, no jumpers to move.  If auto-detection
fails, pass the port explicitly:

```bash
pio run -t upload --upload-port COM4          # Windows
pio run -t upload --upload-port /dev/ttyUSB0  # Linux / macOS
```

### 6. Open the serial monitor

```bash
pio device monitor -b 115200
```

PlatformIO's monitor sends `\n` on Enter by default.  If your terminal
doesn't, set its line ending to **Newline** or **Both NL & CR** so the
`ON` / `OFF` / `BLINK` commands are recognised.

Press the Pro Mini's **reset** button if no output appears.  Quit with
`Ctrl+C` (CLI) or close the panel (VS Code).

> **Only one process can hold the USB port at a time.**  Close the
> monitor before re-running `pio run -t upload`.

---

## Expected Output

Boot:

```
=============================
  KTOS LED Control Example
=============================
Commands: ON, OFF, BLINK
LED BLINK
```

Typing `ON`:

```
Command: ON
LED ON
```

Typing `BLINK`:

```
Command: BLINK
LED BLINK
```

Unknown input:

```
Command: HELLO
Unknown command. Try ON, OFF, BLINK.
```

The on-board LED reflects the current mode in real time.

---

## Troubleshooting

| Symptom                                  | Likely cause                                   | Fix                                                                  |
|------------------------------------------|------------------------------------------------|----------------------------------------------------------------------|
| LED never blinks                         | `led_task` never dispatched (Timer1 missing)   | Confirm `ISR(TIMER1_COMPA_vect)` is present in `main.c`             |
| Commands have no effect                  | Monitor not sending newline                    | Set monitor line ending to "Newline" or "Both NL & CR"              |
| `[KTOS FATAL] T Failed` on boot          | Heap exhausted creating second task            | Drop both stacks to 48 words                                         |
| LED stops responding after some commands | Task queue overflow                            | Increase the `QueueSize` of `led_task` (4 → 8)                       |
| Board resets when typing fast            | Stack overflow inside `uart_task`              | Raise `uart_task` `StackSize` to 96                                  |
| Random characters in monitor              | Wrong baud / U2X miscalc                      | Verify `UBRR0L = 16` and `U2X0 = 1` in `uart_init()`                |

---

## Notes for KDOS Integration

- `uart_get_byte()` / `uart_putc()` → future `ktos_hal_uart_*`.
- `led_on()` / `led_off()` / `led_toggle()` → future `ktos_hal_gpio_*`.
- The split between input task and actuator task **already follows the
  KDOS HAL boundary** — the eventual port will replace only the
  register accesses, not the message contract.
