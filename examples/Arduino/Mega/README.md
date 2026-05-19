### KTOS on Arduino Mega 2560 — Bare-Metal Examples

Six examples that run on the **Arduino Mega 2560 (ATmega2560)** with
**KTOS as the operating system** — no Arduino framework, no Arduino
runtime, no `setup()` / `loop()`.  Every example provides its own
`main()`, talks to USART0, ADC, TWI, SPI, and GPIO through **direct AVR
register access**, and hands the CPU to `ktos_RunOS()` which never
returns.

Where it makes sense, examples are split across **two cooperating
tasks** that communicate exclusively through `ktos_SendMsg()` — exactly
the pattern the ESP8266 KTOS examples use.

---

## Why bare-metal?

The Arduino framework adds overhead for `Serial`, `Wire`, `SPI`,
`analogRead`, and the `loop()` runtime.  On a 256 KB / 8 KB chip
that matters less than on the Nano, but the register-level code is
far more readable and leaves KTOS visibly in charge of the CPU.

| Example                                         | KTOS feature exercised                                         |
|-------------------------------------------------|----------------------------------------------------------------|
| [`uart/`](uart/README.md)                       | **Two tasks**, line-assembler hands lines to command processor |
| [`led_control/`](led_control/README.md)         | **Two tasks**, `ktos_SendMsg(MSG_SET_MODE)` from UART to LED   |
| [`button_control/`](button_control/README.md)   | **Two tasks**, debouncer publishes `MSG_BUTTON_EVENT` to UI    |
| [`adc/`](adc/README.md)                         | Single task, 1 Hz timer wake — `return 1000;`                 |
| [`i2c/`](i2c/README.md)                         | Single task, 5 s timer wake doing TWI scan                     |
| [`spi/`](spi/README.md)                         | Single task, MOSI→MISO loopback self-test every 1 s            |

---

## Board Overview

| Item              | Value                                                       |
|-------------------|-------------------------------------------------------------|
| Board             | Arduino Mega 2560                                           |
| MCU               | ATmega2560 (AVR 8-bit)                                      |
| Clock             | 16 MHz                                                      |
| Logic level       | 5 V                                                         |
| Flash             | 256 KB                                                      |
| SRAM              | 8 KB                                                        |
| EEPROM            | 4 KB                                                        |
| KTOS BSP          | `bsp/atmega2560/`                                           |
| KTOS tick source  | Timer1 CTC, 1 ms                                            |
| Toolchain         | avr-gcc + avr-libc (no Arduino framework)                   |
| Upload protocol   | `wiring` (STK500v2)                                         |
| Monitor baud      | 115200, 8N1                                                 |

> **Important:** The ATmega2560 has a 3-byte (22-bit) program counter.
> The KTOS BSP (`bsp/atmega2560/ktos_bsp.c`) accounts for this in
> `ktos_hal_InitTaskStack()` — the top three bytes of every new task's
> stack frame encode the 22-bit return address correctly.

---

## ATmega2560 → Mega 2560 Pin Map

Several pins differ from the Nano/Uno — these are the ones used by
the examples.

| Mega label  | AVR pin  | Used by                      | Register bit                      |
|-------------|----------|------------------------------|-----------------------------------|
| `D0 / RX0`  | `PE0`    | UART RX (USART0)             | `UDR0` (auto-controlled)          |
| `D1 / TX0`  | `PE1`    | UART TX (USART0)             | `UDR0` (auto-controlled)          |
| `D2`        | `PE4`    | Button (`button_control`)    | `PINE` bit 4 / `PORTE` bit 4      |
| `D13`       | `PB7`    | LED (`led_control`)          | `DDRB` / `PORTB` bit 7            |
| `A0`        | `PF0`    | ADC channel 0 (`adc`)        | `ADMUX` ch 0, read `ADC`          |
| `D20 / SDA` | `PD1`    | I²C SDA (`i2c`)              | `SDA` of TWI peripheral           |
| `D21 / SCL` | `PD0`    | I²C SCL (`i2c`)              | `SCL` of TWI peripheral           |
| `D50`       | `PB3`    | SPI MISO (`spi`)             | `DDRB` / `PORTB` bit 3 (input)    |
| `D51`       | `PB2`    | SPI MOSI (`spi`)             | `DDRB` / `PORTB` bit 2            |
| `D52`       | `PB1`    | SPI SCK  (`spi`)             | `DDRB` / `PORTB` bit 1            |
| `D53`       | `PB0`    | SPI SS   (`spi`)             | `DDRB` / `PORTB` bit 0            |

> **Note:** D13 is **PB7** on the Mega 2560 — not PB5 as on the Uno/Nano.
> D2 is **PE4** on the Mega 2560 — not PD2.
> SCK is **PB1/D52**, so D13 does **not** flicker during SPI transfers.

---

## Common Setup

### 1. Install PlatformIO

**VS Code (recommended):**

1. Install [Visual Studio Code](https://code.visualstudio.com/).
2. Install the **PlatformIO IDE** extension.
3. Reload VS Code when prompted.

**Command line:**

```bash
python3 -m pip install --user platformio
pio --version
```

### 2. Open an example

```bash
cd examples/Arduino/Mega/adc          # or any other example folder
```

In VS Code: `File → Open Folder…` and select the example directory.

### 3. Build and upload

```bash
pio run                 # compile
pio run -t upload       # flash via USB (STK500v2 / wiring protocol)
```

### 4. Open the serial monitor

```bash
pio device monitor -b 115200
```

Press the Mega's reset button if no banner appears.

---

## What gets compiled

Each example's `platformio.ini` is deliberately minimal — **no
framework**, just the AVR platform plus the KTOS sources:

```ini
[env:megaatmega2560]
platform        = atmelavr
board           = megaatmega2560
upload_protocol = wiring
monitor_speed   = 115200
build_src_filter =
    +<*>
    +<../../../../../core/ktos.c>
    +<../../../../../bsp/atmega2560/ktos_bsp.c>
build_flags =
    -Os -Wall -Wextra
    -I../../../../core
    -I../../../../bsp/atmega2560
```

- `core/ktos.c` — the platform-independent KTOS scheduler.
- `bsp/atmega2560/ktos_bsp.c` — Timer1 init, **3-byte PC** context
  switch, stack frame builder.

No Arduino framework is pulled in.  The only runtime is **avr-libc**
which avr-gcc links automatically.

---

## Anatomy of a KTOS bare-metal example

Every `main.c` in this folder follows the same skeleton:

```c
#include "../../../../../core/ktos.h"
#include <avr/io.h>
#include <avr/interrupt.h>

/* 1. Bare-metal peripheral driver(s) — direct register access ------- */
static void uart_init(void)   { /* UBRR0H/L, UCSR0A/B/C */ }
static void uart_putc(char c) { while (!(UCSR0A & (1<<UDRE0))); UDR0 = c; }

/* 2. KTOS platform callbacks ---------------------------------------- */
__attribute__((noreturn))
void ktos_Emergency(const char *msg) {
    uart_puts("[FATAL] "); uart_puts(msg); while (1);
}
void ktos_DebugPrintf(const char *f, ...) { (void)f; }
void ktos_InitSys(void) { }

/* 3. KTOS 1 ms tick — Timer1 CTC, set up by the ATmega2560 BSP ----- */
ISR(TIMER1_COMPA_vect) { ktos_timer_irq_handler(); }

/* 4. Your task(s) --------------------------------------------------- */
static WORD my_task(WORD MsgType, WORD sParam, LONG lParam) {
    switch (MsgType) {
        case KTOS_MSG_TYPE_INIT:  /* one-shot init  */ break;
        case KTOS_MSG_TYPE_TIMER: /* periodic work  */ break;
        /* user messages from ktos_SendMsg() land here */
    }
    return 1000;   /* sleep 1 s then deliver KTOS_MSG_TYPE_TIMER */
}

/* 5. main() — register tasks, give the CPU to KTOS forever ---------- */
int main(void) {
    uart_init();
    ktos_InitTask(my_task, /*stack words=*/96, /*queue=*/4, 'T');
    ktos_RunOS();         /* never returns */
    return 0;             /* unreachable   */
}
```

Key points:

- **No `setup()` / `loop()`.**  KTOS provides the main loop; the
  application provides plain C `main()`.
- **Timer1 is KTOS's tick.**  The ATmega2560 BSP configures Timer1 CTC
  for a 1 ms interrupt; `ISR(TIMER1_COMPA_vect)` hands control to
  `ktos_timer_irq_handler()`.
- **Tasks never block.**  Return a sleep value in ms (or `MSG_WAIT` for
  "wake me only when a message arrives").
- **Cooperative ⇒ no mutexes.**  Two tasks cannot run simultaneously,
  so shared globals need no lock.

---

## Notes on the SPI Loopback Example

The `spi/` example requires a single jumper wire connecting **D51
(MOSI)** to **D50 (MISO)**.  It transmits the pattern
`{0xA5, 0x5A, 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0xFF}` and compares the
received bytes.  Because SCK is on D52 (PB1) rather than D13 (PB7),
the on-board LED does not flicker during transfers.

---

## Troubleshooting

| Symptom                                                | Likely cause                       | Fix                                                                              |
|--------------------------------------------------------|------------------------------------|----------------------------------------------------------------------------------|
| `avrdude: stk500v2_ReceiveMessage(): timeout`          | Wrong upload protocol or port      | Confirm `upload_protocol = wiring` and the correct COM port                      |
| Board not listed by `pio device list`                  | Missing USB driver                 | Install the CH340 driver if using a clone board                                  |
| Garbled serial output                                  | Wrong baud rate                    | Set monitor to **115200**, 8N1                                                   |
| Banner prints but tasks never fire                     | Timer1 ISR not hooked up           | Confirm `ISR(TIMER1_COMPA_vect)` is present in `main.c`                          |
| SPI: `Loopback FAIL`                                   | Jumper missing or reversed         | Connect D51 to D50 with a single wire                                            |
| Button: no events                                      | Wrong pin                          | D2 on the Mega is **PE4**, not PD2 — use `PINE`/`PORTE`/`DDRE`                  |
| I²C scan: all `No devices found`                       | Missing pull-ups                   | Add 4.7 kΩ pull-up resistors from SDA (D20) and SCL (D21) to 5 V                |
| `[KTOS FATAL]` on boot                                 | Heap exhausted creating a task     | Reduce the `StackSize` argument(s) to `ktos_InitTask()`                          |
