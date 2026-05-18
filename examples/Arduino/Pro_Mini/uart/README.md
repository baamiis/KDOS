# KTOS — Arduino Pro Mini UART Example (Bare-Metal)

**Two KTOS tasks** demonstrate the producer / consumer pattern over a
single hardware UART.  One task reads and echoes; the other parses
and responds.  They communicate exclusively through `ktos_SendMsg()`.

KTOS is the operating system — no Arduino framework, no `Serial`, no
`String`.  USART0 is driven directly through its registers.

---

## Overview

```
   USB Serial bytes ──► rx_task ──► (echo + assemble line)
                        ('R')          │
                                       │ ktos_SendMsg(MSG_LINE_READY)
                                       ▼
                                ┌──────────────┐
                                │   cmd_task   │
                                │    ('C')     │── USART0 ──► PONG / INFO / ...
                                └──────────────┘
```

- **`rx_task`** wakes every 10 ms (`return 10`), drains USART0's RX
  FIFO, echoes every byte, and assembles characters into a 64-byte
  buffer.  When `\r` or `\n` arrives it posts `MSG_LINE_READY` to
  `cmd_task`.
- **`cmd_task`** sleeps with `MSG_WAIT` — zero CPU between events.
  On `MSG_LINE_READY` it runs `strcmp` against three commands and
  prints the appropriate reply.

| Command | Reply                                |
|---------|--------------------------------------|
| `PING`  | `PONG`                                |
| `INFO`  | Multi-line board information          |
| `HELP`  | Available commands                    |

Any unknown line is echoed back with `Unknown:` prepended.

---

## What it shows about KTOS

- **Decoupling I/O from logic.**  USART0's transmit busy-wait inside
  `cmd_task` cannot delay the RX path because `rx_task` is scheduled
  independently and reschedules itself every 10 ms.
- **A shared buffer is safe.**  KTOS is cooperative — `cmd_task` reads
  `g_line_buf` between `MSG_LINE_READY` and its `return MSG_WAIT`;
  `rx_task` cannot overwrite the buffer in that window because it
  only runs when `cmd_task` has yielded.  No mutex needed.
- **`ktos_SendMsg()` carries 48 bits of payload** (16-bit `sParam` +
  32-bit `lParam`).  Here we pass the line length as `sParam` so
  `cmd_task` knows how many bytes to read.

---

## Hardware Required

| Item                                  | Quantity |
|---------------------------------------|----------|
| Arduino Pro Mini                  | 1        |
| USB-TTL adapter (FTDI or CH340) with jumper wires | 1       |

No external wiring outside USB.

---

## Wiring

USB only — the example uses the Pro Mini's on-board USB-to-serial bridge.

If you want to talk to the example from an external UART (FTDI, second
MCU, …) instead of USB, wire as follows:

| External device | Arduino label | AVR pin |
|-----------------|------------|---------|
| `TX`            | `RX (D0)`  | `PD0`   |
| `RX`            | `TX (D1)`  | `PD1`   |
| `GND`           | `GND`      | —       |

> Note: D0/D1 are shared with the USB bridge.  Disconnect external
> wires before reflashing or you will block the bootloader.

---

## How It Works

1. `main()` calls `uart_init()` (USART0 at 115200 baud, 8N1) and
   creates **two** KTOS tasks before `ktos_RunOS()`.

2. `cmd_task` is created first so `rx_task` has a valid handle.

3. On `KTOS_MSG_TYPE_INIT`:
   - `cmd_task` prints the banner and returns `MSG_WAIT`.
   - `rx_task` clears its line-assembly state and returns `10`.

4. Every 10 ms `rx_task` wakes with `KTOS_MSG_TYPE_TIMER`:
   - Loop until `UCSR0A & (1<<RXC0)` is clear.
   - Echo every byte, normalising `\r` to `\r\n`.
   - Append non-newline bytes (upper-cased) into `g_line_buf` until
     `LINE_BUF_SIZE - 1 = 63` is hit.
   - On a newline, terminate the buffer and call:

     ```c
     ktos_SendMsg(g_cmd_task, MSG_LINE_READY, (WORD)len, 0);
     ```

5. KTOS marks `cmd_task` ready and dispatches it on the next
   scheduling pass.  It runs `strcmp`, writes the response, and
   returns `MSG_WAIT`.

The receive buffer is fixed-size and never allocated.  Lines longer
than 63 characters are silently truncated — the buffer cannot overrun.

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
cd KTOS/examples/Arduino/Pro_Mini/uart
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

Make sure your monitor sends a newline (`\n`) or carriage return (`\r`)
when you press Enter — the command parser needs it.

Press the Pro Mini's **reset** button if no output appears.  Quit the
monitor with `Ctrl+C` (CLI) or close the panel (VS Code).

> **Only one process can hold the USB port at a time.**  Close the
> monitor before re-running `pio run -t upload`.

---

## Expected Output

Boot-up:

```
=============================
  KTOS UART Example
=============================
Type HELP for commands.
```

Typing `PING`:

```
PING
PONG
```

Typing `INFO`:

```
INFO
Board   : Arduino Pro Mini (ATmega328P)
Clock   : 16 MHz
SRAM    : 2 KB
Flash   : 32 KB
Baud    : 115200, 8N1
OS      : KTOS
```

Typing `HELP`:

```
HELP
Commands:
  PING  - reply with PONG
  INFO  - show board info
  HELP  - this list
```

Typing something unknown:

```
HELLO
Unknown: HELLO
Type HELP for commands.
```

---

## Troubleshooting

| Symptom                              | Likely cause                                      | Fix                                                       |
|--------------------------------------|---------------------------------------------------|-----------------------------------------------------------|
| No banner after upload               | Serial monitor not yet open                       | Open the monitor and reset the Pro Mini                       |
| Each character echoed twice          | Monitor has local echo enabled                    | Disable local echo in your terminal                       |
| Commands not recognised              | No newline at end of line                         | Pick "Newline" in the serial monitor                      |
| Garbled characters                   | Wrong baud rate                                   | Set monitor to **115200**, 8N1                           |
| Bootloader fails on next upload      | External UART still wired to D0/D1                | Disconnect external TX/RX before flashing                 |
| Banner prints but commands never reply | Timer1 ISR not hooked up                        | Confirm `ISR(TIMER1_COMPA_vect)` is in `main.c`           |

---

## Notes for KDOS Integration

- `uart_putc()`, `uart_puts()`, `uart_get_byte()` map directly to
  `ktos_hal_uart_write()` / `ktos_hal_uart_read()`.
- The split between `rx_task` (line assembly) and `cmd_task` (parsing)
  is the canonical shape for a future KDOS shell — adding new commands
  is a `strcmp` chain inside `cmd_task` with no risk of blocking RX.
- Avoid pulling in `printf`/`scanf` if you port this — they add ~2 KB
  of flash on the ATmega328P.  Tiny direct UART writes like these
  keep the binary under 5 KB.
