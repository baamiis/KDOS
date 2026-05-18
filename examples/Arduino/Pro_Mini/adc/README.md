# KTOS — Arduino Pro Mini ADC Example (Bare-Metal)

A **single KTOS task** samples a potentiometer on **A0** every second
and prints the raw 10-bit count plus the calculated voltage.  KTOS is
the operating system — no Arduino framework, no `analogRead()`, no
`Serial`.  The ADC and USART0 are driven directly through AVR registers.

---

## Overview

```
              ┌─────────────────────────────────────┐
              │             adc_task                │
              ├─────────────────────────────────────┤
INIT  ───────►│ print banner                        │
              │                                     │
TIMER ◄──┐    │ raw = adc_read(channel 0);          │
   1 s   │    │ mv  = raw * 5000 / 1023;            │
         │    │ uart_puts("A0 raw=... voltage=...") │
         │    └──────────────┬──────────────────────┘
         │                   │
         └── return 1000; ───┘   (KTOS reschedules after 1 s)
```

The whole application is `adc_task()` plus the bare-metal ADC and
USART0 drivers at the top of `main.c`.  `main()` runs only long enough
to initialise both peripherals, register the task, and call
`ktos_RunOS()` — which never returns.

---

## What it shows about KTOS

- A task is a plain function with signature
  `WORD task(WORD MsgType, WORD sParam, LONG lParam)`.
- The **return value is a sleep duration in milliseconds**.
  `return 1000` → wake me in one second with `KTOS_MSG_TYPE_TIMER`.
- `KTOS_MSG_TYPE_INIT` is delivered exactly once, on the first dispatch.
- The 1 ms tick comes from **Timer1 CTC** in the AVR BSP — Timer0 and
  Timer2 remain free for application use.
- No Arduino runtime is present: the binary is ~3 KB of flash and uses
  under 600 bytes of SRAM.

---

## Hardware Required

| Item                                | Quantity |
|-------------------------------------|----------|
| Arduino Pro Mini                | 1        |
| 10 kΩ potentiometer (any value)     | 1        |
| Breadboard + jumper wires           | —        |

---

## Wiring

| Potentiometer pin | Arduino label | AVR pin |
|-------------------|------------|---------|
| End 1             | `5V`       | —       |
| Wiper (middle)    | `A0`       | `PC0` (ADC0) |
| End 2             | `GND`      | —       |

```
   5V ──┐
        │
       [ POT ]── wiper ──> A0 (PC0)
        │
  GND ──┘
```

> Do **not** leave A0 floating: the ADC will read random noise.

---

## How It Works

1. `main()` runs on the avr-libc startup stack.  It calls `uart_init()`
   (sets `UBRR0L = 16`, `U2X0`, `TXEN0/RXEN0`, 8N1) and `adc_init()`
   (sets AVcc reference in `ADMUX`, enables ADC with /128 prescaler in
   `ADCSRA`).
2. `ktos_InitTask(adc_task, 96, 4, 'A')` allocates a 384-byte stack,
   a 4-message queue, and a TCB.
3. `ktos_RunOS()` configures Timer1 CTC for 1 ms (via the BSP) and
   transfers control to `adc_task` with `MsgType = KTOS_MSG_TYPE_INIT`.
4. `adc_task` prints the banner, samples the ADC, prints the line, and
   returns `1000`.
5. The Timer1 ISR (`TIMER1_COMPA_vect → ktos_timer_irq_handler`) ticks
   the task's countdown every millisecond.  When it hits zero the
   scheduler redispatches the task with `KTOS_MSG_TYPE_TIMER`.

The voltage is printed in millivolts (`raw × 5000 / 1023`) and
formatted manually as `X.XXV` — this avoids pulling in avr-libc's
floating-point `printf`, which is too large for the Pro Mini's flash.

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
cd KTOS/examples/Arduino/Pro_Mini/adc
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

Press the Pro Mini's **reset** button if no output appears.  Quit the
monitor with `Ctrl+C` (CLI) or close the panel (VS Code).

> **Only one process can hold the USB port at a time.**  Close the
> monitor before re-running `pio run -t upload`.

---

## Expected Output

```
=============================
  KTOS ADC Example
=============================
Reading A0 every 1 s...
A0 raw=512 voltage=2.50V
A0 raw=480 voltage=2.34V
A0 raw=1023 voltage=4.99V
A0 raw=0 voltage=0.00V
```

Turning the potentiometer sweeps the `raw` value smoothly between `0`
and `1023`.  The displayed voltage is rounded to two decimals (the
trailing 1 LSB error at full scale is `5000/1023 ≈ 5 mV`).

---

## Troubleshooting

| Symptom                            | Likely cause                            | Fix                                                       |
|------------------------------------|-----------------------------------------|-----------------------------------------------------------|
| Banner appears but no further lines | Timer1 not wired to KTOS               | Check `ISR(TIMER1_COMPA_vect)` is present in `main.c`     |
| Readings stuck near `0` or `1023`   | Wiper miswired                          | Re-check the wiring table                                 |
| Voltage shows ≈ `0.49` × actual     | AVcc reference is not 5 V               | Power the Pro Mini from USB or a regulated 5 V source         |
| `[KTOS FATAL] T Failed` on boot     | Heap exhausted creating the task        | Drop `StackSize` to 64                                    |
| Compiles but resets in a loop       | Stack overflow inside `adc_task`        | Raise `StackSize` to 128                                  |
| Garbled characters in monitor       | Wrong baud or U2X miscalc               | Verify `UBRR0L = 16` and `U2X0 = 1` in `uart_init()`      |

---

## Notes for KDOS Integration

- `adc_init()` + `adc_read()` will become `ktos_hal_adc_init()` /
  `ktos_hal_adc_read()`.
- `uart_init()` / `uart_putc()` / `uart_puts()` will become
  `ktos_hal_uart_*()`.
- The 1 Hz timing already *is* a KTOS task return value — nothing to
  change once the peripheral HAL lands.
