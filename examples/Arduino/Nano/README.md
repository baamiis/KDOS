### KTOS on Arduino Nano — Bare-Metal Examples

Five examples that run on the **Arduino Nano Classic (ATmega328P)** with
**KTOS as the operating system** — no Arduino framework, no Arduino
runtime, no `setup()` / `loop()`.  Every example provides its own
`main()`, talks to USART0, ADC, TWI, and GPIO through **direct AVR
register access**, and hands the CPU to `ktos_RunOS()` which never
returns.

Where it makes sense, examples are split across **two cooperating
tasks** that communicate exclusively through `ktos_SendMsg()` — exactly
the pattern the ESP8266 KTOS examples use.

---

## Why bare-metal?

The Arduino framework adds ~1–2 KB of flash and several hundred bytes
of SRAM for `Serial`, `Wire`, `analogRead`, and the `loop()` runtime.
On a 32 KB / 2 KB chip that overhead matters — and it also conceals
exactly what KTOS is doing.  These examples replace it all with a
handful of register writes you can read in under a minute, leaving
KTOS visibly in charge.

| Example                                     | KTOS feature exercised                                          |
|---------------------------------------------|------------------------------------------------------------------|
| [`adc/`](adc/README.md)                     | Single task, 1 Hz timer wake — `return 1000;`                    |
| [`i2c/`](i2c/README.md)                     | Single task, 5 s timer wake doing TWI scan                       |
| [`led_control/`](led_control/README.md)     | **Two tasks**, `ktos_SendMsg(MSG_SET_MODE)` from UART to LED     |
| [`button_control/`](button_control/README.md) | **Two tasks**, debouncer publishes `MSG_BUTTON_EVENT` to UI    |
| [`uart/`](uart/README.md)                   | **Two tasks**, line-assembler hands lines to a command processor |

---

## Anatomy of a KTOS bare-metal example

Every `main.c` in this folder follows the same skeleton:

```c
#include "../../../../core/ktos.h"
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

/* 3. KTOS 1 ms tick — Timer1 CTC, set up by the AVR BSP ------------- */
ISR(TIMER1_COMPA_vect) { ktos_timer_irq_handler(); }

/* 4. Your task(s) --------------------------------------------------- */
static WORD my_task(WORD MsgType, WORD Param1, LONG Param2) {
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

- **No `setup()` / `loop()`.**  KTOS provides the main loop in
  `ktos_SwitchTask()`; the application provides plain C `main()`.
- **Timer1 is KTOS's tick.**  The AVR BSP configures Timer1 CTC for a
  1 ms interrupt; `ISR(TIMER1_COMPA_vect)` in the application hands
  control to `ktos_timer_irq_handler()`.
- **Tasks never block.**  Return a sleep value in ms (or `KTOS_MSG_SLEEP_INDEFINITLY` for
  "wake me only when a message arrives").
- **Cooperative ⇒ no mutexes.**  Two tasks cannot run simultaneously,
  so a shared global between them needs no lock.

---

## Board Overview

| Item              | Value                                                  |
|-------------------|--------------------------------------------------------|
| Board             | Arduino Nano Classic                                   |
| MCU               | ATmega328P (AVR 8-bit)                                 |
| Clock             | 16 MHz                                                 |
| Logic level       | 5 V                                                    |
| Flash             | 32 KB (≈30 KB usable)                                  |
| SRAM              | 2 KB                                                   |
| EEPROM            | 1 KB                                                   |
| KTOS BSP          | [`bsp/atmega328p/`](../../../bsp/atmega328p/)          |
| KTOS tick source  | Timer1 CTC, 1 ms                                       |
| Toolchain         | avr-gcc + avr-libc (no Arduino framework)              |
| Upload            | USB bootloader (`-c arduino` / STK500v1)               |
| Monitor baud      | 115200, 8N1                                            |

---

## ATmega328P → Nano Pin Map

The "D2"/"A0"/etc. labels printed on the Nano are aliases for AVR port
pins.  These examples address registers directly, so this table is the
one you actually need.

| Nano label | AVR pin | Used by                | Register bit                     |
|------------|---------|------------------------|----------------------------------|
| `D0 / RX`  | `PD0`   | UART RX (USART0)       | `UDR0` (auto-controlled)         |
| `D1 / TX`  | `PD1`   | UART TX (USART0)       | `UDR0` (auto-controlled)         |
| `D2`       | `PD2`   | Button (`button_control`) | `PIND` bit 2 / `PORTD` bit 2 |
| `D13`      | `PB5`   | LED (`led_control`)    | `DDRB` / `PORTB` bit 5           |
| `A0`       | `PC0`   | ADC channel 0 (`adc`)  | `ADMUX` ch 0, read `ADC`         |
| `A4`       | `PC4`   | I²C SDA (`i2c`)        | `SDA` of TWI peripheral          |
| `A5`       | `PC5`   | I²C SCL (`i2c`)        | `SCL` of TWI peripheral          |

---

## Common Setup

### 1. Install PlatformIO

**VS Code (recommended for beginners):**

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
cd examples/Arduino/Nano/adc          # or any other example folder
```

In VS Code: `File → Open Folder…` and select the example directory.

### 3. Build and upload

```bash
pio run                 # compile (pulls in core/ktos.c + bsp/atmega328p/ktos_bsp.c automatically)
pio run -t upload       # flash via USB bootloader
```

### 4. Open the serial monitor

```bash
pio device monitor -b 115200
```

Press the Nano's reset button if no banner appears.

---

## What gets compiled

Each example's `platformio.ini` is deliberately minimal — **no
framework**, just the AVR platform plus the KTOS sources:

```ini
[env:nanoatmega328]
platform        = atmelavr
board           = nanoatmega328
upload_protocol = arduino
monitor_speed   = 115200

build_flags =
    -Os
    -Wall
    -Wextra
    -I../../../../core
    -I../../../../bsp/atmega328p

build_src_filter =
    +<*>
    +<../../../../../core/ktos.c>
    +<../../../../../bsp/atmega328p/ktos_bsp.c>
```

- `core/ktos.c` — the platform-independent KTOS scheduler.
- `bsp/atmega328p/ktos_bsp.c` — Timer1 init, context switch, stack frame
  builder.
- `core/ktos_multi.c` is **deliberately excluded** — it defines its own
  `main()` and stub callbacks that would collide with each example.

No Arduino framework is pulled in.  The only runtime is **avr-libc**
(for `malloc`, `memset`, etc.) which avr-gcc links automatically.

---

## Notes on Nano Limitations

- **Only 2 KB SRAM.**  Each KTOS task costs a TCB (~30 bytes) plus its
  stack (we use 64–128 *words* = 256–512 bytes) plus its message queue
  (4 messages × 8 bytes).  Two tasks ≈ 700–900 B of RAM.
- **Heap is used only once at boot.**  `ktos_InitTask()` calls
  `malloc`/`calloc` for each task; after that the application makes no
  further allocations.  Don't add any either.
- **Single hardware UART.**  Uploading and the serial monitor share the
  same USB port — close the monitor before flashing.
- **No native USB.**  Programming uses the on-board USB-to-serial chip
  (FTDI or CH340).  Install CH340 drivers if your Nano clone is not
  detected.
- **5 V logic.**  Use level shifters for 3.3 V-only sensors.
- **Timer1 is owned by KTOS.**  Timer0 and Timer2 are free for the
  application.

---

## KDOS HAL roadmap

These examples already use direct register access, which is what the
future KTOS peripheral HAL will wrap.  Each `main.c` marks the points
where those HAL calls will slot in:

| Direct register write                          | Future KTOS HAL                                 |
|------------------------------------------------|-------------------------------------------------|
| `ADMUX`, `ADCSRA`, read `ADC`                  | `ktos_hal_adc_read()`                           |
| `DDRx`, `PORTx`, read `PINx`                   | `ktos_hal_gpio_*()`                             |
| `UBRR0`, `UCSR0A/B/C`, `UDR0`                  | `ktos_hal_uart_*()`                             |
| `TWBR`, `TWCR`, `TWSR`, `TWDR`                 | `ktos_hal_i2c_*()`                              |

---

## Troubleshooting (applies to every example)

| Symptom                                                | Likely cause                       | Fix                                                                          |
|--------------------------------------------------------|------------------------------------|------------------------------------------------------------------------------|
| `avrdude: stk500_recv(): programmer is not responding` | Old bootloader / wrong baud        | Add `board_upload.speed = 57600` to `platformio.ini`                          |
| Board not listed by `pio device list`                  | Missing USB-to-serial driver       | Install CH340 (clones) or FTDI drivers                                       |
| Garbled serial output                                  | Wrong baud rate                    | Set monitor to **115200**, 8N1                                               |
| `pio` command not found                                | PlatformIO not on PATH             | Re-open the terminal or add `~/.platformio/penv/bin` to `PATH`               |
| Upload succeeds but sketch never runs                  | Sharing port with monitor          | Close the serial monitor before `pio run -t upload`                          |
| Banner prints but tasks never fire                     | Timer1 ISR not hooked up           | Confirm `ISR(TIMER1_COMPA_vect)` is present in `main.c`                      |
| `[KTOS FATAL] T Failed` on boot                        | Heap exhausted creating a task     | Reduce `StackSize` argument(s) to `ktos_InitTask()`                          |
| Random resets / brown-out                              | Underpowered USB hub               | Power the Nano from a powered hub or the 5 V pin                             |
