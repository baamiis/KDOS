# KTOS - Arduino Uno EEPROM Example (Bare-Metal)

Use the ATmega328P's **1 KB of internal EEPROM** as non-volatile
storage.  This example combines two classic patterns into one program:

- A **boot counter** at EEPROM `0..1` that increments every reset and
  survives power cycles - **power-cycle the Uno and watch the count
  rise**.
- An **interactive shell** (`READ` / `WRITE` / `DUMP`) so you can
  inspect and edit EEPROM contents live over USB-serial.

Two KTOS tasks, no Arduino framework, direct register access for
`EEAR` / `EEDR` / `EECR` and USART0.

---

## Overview

```
   Power-on ──► counter_task reads EEPROM[0..1], increments, writes back.
                  ('E')
                  │
                  ├── return 5000;  ── KTOS_MSG_TYPE_TIMER every 5 s
                  │                    └─► print "Boot #N, uptime XX s"
                  │
                  ▲
                  │ MSG_LINE_READY  (READ / WRITE / DUMP / INFO / HELP)
                  │
   USB Serial ──► rx_task drains USART0, assembles a line, posts MSG_LINE_READY.
                  ('R')
```

- **`counter_task`** owns the EEPROM, the in-RAM boot counter, and the
  uptime clock.  It handles three different message types in one task:
  - `KTOS_MSG_TYPE_INIT` - boot bookkeeping.
  - `KTOS_MSG_TYPE_TIMER` - heartbeat.
  - `MSG_LINE_READY` - command from `rx_task`.
- **`rx_task`** drains USART0 every 10 ms, echoes each byte, assembles
  a line in a fixed 40-byte buffer, and on `\r`/`\n` posts
  `MSG_LINE_READY`.

A single task can be both periodic and event-driven: `counter_task`
returns `5000` after every dispatch, so the next wake is either the
heartbeat *or* a command - whichever arrives first.

---

## What it shows about KTOS

- **One task, multiple roles.**  `counter_task` is the timekeeper *and*
  the shell.  Splitting it across two tasks would have been overkill -
  the message types are orthogonal and the handler is short.
- **Non-blocking long operations.**  An EEPROM write takes about 3.4 ms.
  We do not block waiting for it - the helper `eeprom_wait_ready()` at
  the top of every write/read serialises against any in-flight write,
  but the CPU stays free to handle USART0 polling in `rx_task` in the
  meantime.
- **Static data lives outside SRAM.**  The boot counter survives
  resets *and* power cycles - SRAM doesn't.  EEPROM is the right home
  for it.

---

## Commands

| Command                  | Effect                                              |
|--------------------------|-----------------------------------------------------|
| `HELP`                   | List commands                                       |
| `INFO`                   | Show current boot count + uptime                    |
| `READ <addr>`            | Read one byte at decimal address `0..1023`          |
| `WRITE <addr> <val>`     | Write one byte (`val` decimal 0..255)               |
| `DUMP`                   | Hex dump of the first 64 EEPROM bytes               |

Commands are case-insensitive.

---

## Hardware Required

| Item                 | Quantity |
|----------------------|----------|
| Arduino Uno R3       | 1        |
| USB cable            | 1        |

No external wiring.  The EEPROM is inside the MCU.

---

## How It Works

### Boot counter (the persistent bit)

On `KTOS_MSG_TYPE_INIT`, `counter_task` runs:

```c
uint16_t cnt = eeprom_read16(EEPROM_BOOT_COUNTER_ADDR);
if (cnt == 0xFFFFU) { cnt = 0; }     /* factory blank */
++cnt;
eeprom_write16(EEPROM_BOOT_COUNTER_ADDR, cnt);
g_boot_count = cnt;
```

- A pristine ATmega328P EEPROM cell reads `0xFF`, so a fresh chip's
  16-bit counter is `0xFFFF`.  We treat that as "first run" and reset
  to zero before incrementing.
- After 65535 boots the counter overflows back to 0 and rolls over -
  no big deal for a demo.

### EEPROM access (the register dance)

A read is straightforward - the hardware halts the CPU for 4 cycles:

```c
EEAR = addr;
EECR |= (1 << EERE);
value = EEDR;
```

A write requires a specific atomic sequence:

```c
uint8_t sreg = SREG;
cli();                     /* sequence below must run uninterrupted */
EEAR = addr;
EEDR = value;
EECR |= (1 << EEMPE);      /* master write enable */
EECR |= (1 << EEPE);       /* MUST be within 4 cycles of EEMPE */
SREG = sreg;
```

After `EEPE` is set the cell takes ~3.4 ms to update - we do **not**
block waiting.  The next call's `eeprom_wait_ready()` (which polls
`EEPE`) handles serialisation.

### Heartbeat + commands in one task

`counter_task` always returns `5000`, which means *either*:

- 5 s elapses with no input → `KTOS_MSG_TYPE_TIMER` arrives → uptime
  prints; or
- A command arrives sooner → `MSG_LINE_READY` is delivered, handled,
  and the 5 s timer restarts.

Either way, the task wakes exactly once and goes back to sleep.

---

## Download, Build, and Flash

### 1. Prerequisites

- **PlatformIO** - install the [VS Code extension](https://platformio.org/install/ide?install=vscode), or the CLI:
  ```bash
  python3 -m pip install --user platformio
  pio --version
  ```
- **USB-serial driver** - genuine Unos work out of the box.  Clones
  with the **CH340** chip need the
  [WCH CH340 driver](https://www.wch.cn/downloads/CH341SER_EXE.html)
  on Windows / macOS.

### 2. Get the source

```bash
git clone https://github.com/baamiis/KTOS.git
cd KTOS/examples/Arduino/Uno/eeprom
```

(Or download the repository as a ZIP from GitHub and `cd` into the
same folder.)

### 3. Connect the Uno and find its port

```bash
pio device list
```

| OS       | Typical port                     |
|----------|----------------------------------|
| Linux    | `/dev/ttyACM0` (genuine) or `/dev/ttyUSB0` (clone) |
| macOS    | `/dev/cu.usbmodem14101`          |
| Windows  | `COM4`                           |

### 4. Build

```bash
pio run
```

PlatformIO pulls `core/ktos.c` and `bsp/atmega328p/ktos_bsp.c` into
the build automatically.  Output: `.pio/build/uno/firmware.elf` (and `.hex`).

### 5. Flash

```bash
pio run -t upload
```

If auto-detection fails, pass the port explicitly:

```bash
pio run -t upload --upload-port COM4          # Windows
pio run -t upload --upload-port /dev/ttyACM0  # Linux / macOS
```

### 6. Open the serial monitor

```bash
pio device monitor -b 115200
```

> **Only one process can hold the USB port at a time.**  Close the
> monitor before re-running `pio run -t upload`.

---

## Expected Output

First boot on a fresh chip (factory-blank EEPROM):

```
=============================
  KTOS EEPROM Example
=============================
Boot #1, uptime 0 s
Type HELP for commands.
Boot #1, uptime 5 s
Boot #1, uptime 10 s
...
```

Power-cycle (unplug, replug) and reset:

```
=============================
  KTOS EEPROM Example
=============================
Boot #2, uptime 0 s
Type HELP for commands.
```

A short interactive session:

```
HELP
Commands:
  HELP                this list
  INFO                boot count + uptime
  READ <addr>         read one byte (addr 0..1023)
  WRITE <addr> <val>  write one byte (val 0..255)
  DUMP                show first 64 EEPROM bytes
READ 0
EEPROM[0] = 5 (0x05)
WRITE 100 42
Wrote EEPROM[100] = 42
READ 100
EEPROM[100] = 42 (0x2A)
DUMP
EEPROM 0..63:
00: 05 00 FF FF FF FF FF FF FF FF FF FF FF FF FF FF
10: FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF
20: FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF
30: FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF
```

> Address `0x00` shows the low byte of the boot counter (5 here), and
> `0x01` is its high byte (0).  Bytes you haven't written are factory
> `0xFF`.

---

## Troubleshooting

| Symptom                              | Likely cause                                  | Fix                                                       |
|--------------------------------------|-----------------------------------------------|-----------------------------------------------------------|
| Boot count never advances            | Bootloader is performing a chip-erase on every flash | Use `pio run -t upload`, not Arduino IDE "Burn Bootloader" |
| `READ 0` always returns `0xFF`       | Counter never written (init handler skipped) | Confirm `ISR(TIMER1_COMPA_vect)` is present in `main.c`   |
| Commands not recognised              | No newline at end of line                     | Set monitor line ending to "Newline" or "CRLF"            |
| `WRITE` silently fails                | Address > 1023 or value > 255                 | Stay in the valid ranges shown by `HELP`                  |
| `[KTOS FATAL] T Failed` on boot      | Heap exhausted creating second task           | Drop `counter_task` `StackSize` to 80                     |
| Boot count jumps by huge amounts     | Brown-out resets during power instability     | Power the Uno from a clean source (powered hub or 9 V jack) |

---

## Notes for KDOS Integration

- `eeprom_read()` / `eeprom_write()` map directly to a future
  `ktos_hal_nvm_read/write()` once a KDOS non-volatile-memory HAL
  exists.
- The shell pattern (rx_task + worker task) is identical to the Nano
  `uart` example - new EEPROM commands are one `else if` branch in
  `counter_task` away.
- The "single task, multiple message types" pattern is a useful KTOS
  idiom for any device that wants both a heartbeat and an event API.

---

## EEPROM longevity

The ATmega328P datasheet rates each EEPROM cell at **100 000 erase /
write cycles**.  This example writes 2 bytes per boot - even at one
boot per minute, that's a century before the boot counter cell wears
out.

If you build something that writes more aggressively, consider:

- **Wear levelling**: rotate the active counter across many addresses.
- **Read-modify-skip**: only call `eeprom_write()` when the byte
  actually needs to change (the AVR hardware does this internally, but
  rewriting the same value still counts toward the erase cycle limit).
