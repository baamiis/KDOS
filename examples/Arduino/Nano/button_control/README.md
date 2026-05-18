# KTOS — Arduino Nano Button Control Example (Bare-Metal)

**Two KTOS tasks** cooperate to handle a noisy mechanical push-button
on **D2** without any of the usual `delay()` traps:

- A polling task samples the pin and applies a 30 ms settled-state
  debounce.
- A UI task waits silently until an edge is confirmed, then prints
  exactly one line per transition.

KTOS is the operating system.  No Arduino framework, no `digitalRead`,
no `Serial` — both peripherals are driven through ATmega328P registers.

---

## Overview

```
   PD2 input ──► button_task ──► ktos_SendMsg(MSG_BUTTON_EVENT, pressed)
                  ('B')           │
                                  ▼
                          ┌──────────────┐
                          │   ui_task    │
                          │    ('I')     │── USART0 ──► "Button pressed"
                          └──────────────┘
```

- **`button_task`** wakes every 5 ms (`return 5`), samples `PIND` bit 2,
  and walks a small state machine.  Only when the line has been stable
  for 30 ms (6 successive identical samples) does it commit the change
  and `ktos_SendMsg(MSG_BUTTON_EVENT)`.
- **`ui_task`** sleeps with `MSG_WAIT` — it consumes **zero CPU** until
  a confirmed event arrives.

Because KTOS is cooperative, USART0's transmit busy-wait inside
`ui_task` cannot delay `button_task`'s 5 ms poll: the polling task
runs first, schedules itself to wake in 5 ms, and only then does
`ui_task` start printing.

---

## What it shows about KTOS

- **Cooperative is not the same as polled.**  `ui_task` is purely
  event-driven (`MSG_WAIT`), wakes only on `MSG_BUTTON_EVENT`, and
  never burns CPU.
- **A debouncer is a state machine, not a delay.**  It returns
  `POLL_PERIOD_MS` (5) on every dispatch — KTOS guarantees the next
  call is exactly 5 ms later, no jitter from `delay()`.
- **Posting from a non-ISR task is the same call as posting from an
  ISR** — `ktos_SendMsg()` is the only mechanism either way.

---

## Hardware Required

| Item                                | Quantity |
|-------------------------------------|----------|
| Arduino Nano Classic                | 1        |
| Momentary push-button (any tactile switch) | 1 |
| Breadboard + jumper wires           | —        |

No external resistor is needed — the example enables the ATmega328P's
internal pull-up (≈ 20–50 kΩ) on PD2.

---

## Wiring

| Button pin | Nano label | AVR pin |
|------------|------------|---------|
| Terminal 1 | `D2`       | `PD2`   |
| Terminal 2 | `GND`      | —       |

```
   PD2 ────────┬───── Button ───── GND
               │
               └─ internal pull-up (DDRD bit 2 = 0, PORTD bit 2 = 1)
```

> The button connects D2 to **GND when pressed**.  Idle = HIGH (via the
> internal pull-up), pressed = LOW.

---

## How It Works

1. `main()` calls `uart_init()` (USART0 at 115200 baud, 8N1) and
   creates **two** KTOS tasks before `ktos_RunOS()`.

2. `ui_task` is created first so `button_task` has a valid `struct
   ktos_TASK *` to send to.  Both task handles are stored in a `static`
   global.

3. On `KTOS_MSG_TYPE_INIT`:
   - `button_task` configures PD2 as input + pull-up
     (`DDRD &= ~(1<<2); PORTD |= (1<<2);`), reads the pin once to
     anchor the initial state, and returns `5`.
   - `ui_task` prints the banner and returns `MSG_WAIT` — it now
     consumes zero CPU.

4. Every 5 ms `button_task` wakes with `KTOS_MSG_TYPE_TIMER`:
   - Read `PIND & (1<<2)`.
   - If the level changed since the last sample, restart the debounce
     counter.
   - If it has been stable for `DEBOUNCE_MS / POLL_PERIOD_MS = 6`
     consecutive samples and differs from the last *committed* state,
     commit the new state and call:

     ```c
     ktos_SendMsg(g_ui_task, MSG_BUTTON_EVENT, pressed ? 1 : 0, 0);
     ```

5. KTOS marks `ui_task` ready and dispatches it on the next
   scheduling pass.  `ui_task` prints `Button pressed` or
   `Button released` and returns `MSG_WAIT` again.

---

## Download, Build, and Flash

### 1. Prerequisites

- **PlatformIO** — install the [VS Code extension](https://platformio.org/install/ide?install=vscode), or the CLI:
  ```bash
  python3 -m pip install --user platformio
  pio --version
  ```
- **USB-serial driver** — most Nano *clones* use the CH340 chip and need the
  [WCH CH340 driver](https://www.wch.cn/downloads/CH341SER_EXE.html) on Windows / macOS.
  Official Nanos with an FTDI chip work out of the box.

### 2. Get the source

```bash
git clone https://github.com/baamiis/KTOS.git
cd KTOS/examples/Arduino/Nano/button_control
```

(Or download the repository as a ZIP from GitHub and `cd` into the same folder.)

### 3. Connect the Nano and find its port

Plug the Nano into your computer with a USB cable, then:

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

PlatformIO talks to the Nano's USB bootloader via `avrdude -c arduino`
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

Press the Nano's **reset** button if no output appears.  Quit the
monitor with `Ctrl+C` (CLI) or close the panel (VS Code).

> **Only one process can hold the USB port at a time.**  Close the
> monitor before re-running `pio run -t upload`.

---

## Expected Output

```
=============================
  KTOS Button Control Example
=============================
Press the button on D2...
Button pressed
Button released
Button pressed
Button released
```

Holding the button down produces a single `Button pressed` line — no
repeated events while held.

---

## Troubleshooting

| Symptom                              | Likely cause                                       | Fix                                                       |
|--------------------------------------|----------------------------------------------------|-----------------------------------------------------------|
| Multiple events per press            | Debounce window too short for your switch          | Raise `DEBOUNCE_MS` to 50 or 80                            |
| `Button pressed` on boot with no press | Button wired across the wrong terminal pair      | A tactile switch has two electrical pairs — rotate it 90° |
| Nothing happens when pressed         | Wired to `5V` instead of `GND`                     | The button must connect D2 to **GND** for active-low      |
| `Button released` only, never pressed | Pull-up not enabled                               | Confirm `PORTD |= (1<<PD2)` in `button_init()`            |
| Banner appears but no events         | Timer1 ISR not hooked up                           | Confirm `ISR(TIMER1_COMPA_vect)` is present in `main.c`   |

---

## Notes for KDOS Integration

- `button_init()` and `button_raw_high()` map cleanly to
  `ktos_hal_gpio_set_mode()` and `ktos_hal_gpio_read_pin()`.
- The 5 ms poll is already a KTOS task return value.
- A future revision should expose the PCINT18 (PD2) pin-change
  interrupt through a KTOS event so the CPU can sleep between presses.
