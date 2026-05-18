### KTOS on Arduino Boards

KTOS bare-metal examples for the classic Arduino board family — KTOS
**replaces** the Arduino OS rather than running on top of it.  Every
example provides its own `main()`, drives peripherals through direct
register access, and hands the CPU to `ktos_RunOS()` which never
returns.

---

## Supported Boards

| Folder                       | Board                  | MCU         | SRAM   | Flash  | Status   |
|------------------------------|------------------------|-------------|--------|--------|----------|
| [`Nano/`](Nano/README.md)    | Arduino Nano Classic   | ATmega328P  | 2 KB   | 32 KB  | 5 examples (adc, i2c, led_control, button_control, uart) |
| [`Uno/`](Uno/README.md)      | Arduino Uno R3         | ATmega328P  | 2 KB   | 32 KB  | All 5 Nano examples run with `board = uno`; **2 Uno-specific** (usb_serial, eeprom) |
| [`Pro_Mini/`](Pro_Mini/README.md) | Arduino Pro Mini  | ATmega328P  | 2 KB   | 32 KB  | 5 Nano-equivalent examples + 1 Pro Mini-specific (`spi`); programmed via external USB-TTL adapter |
| [`Due/`](Due/README.md)      | Arduino Due            | SAM3X8E (Cortex-M3) | 96 KB | 512 KB | 5 examples (adc, i2c, led_control, button_control, uart); 3.3 V logic; KTOS tick on TC0 |

The first three boards share the **ATmega328P** MCU and the same KTOS
BSP ([`bsp/atmega328p/`](../../bsp/atmega328p/)), so most example code
is portable between them with only minor adjustments (pin numbers,
upload protocol, brown-out fuses).

The Arduino **Due** is a different beast — Cortex-M3 with 50× the RAM
and 16× the flash of the AVR boards, 3.3 V logic, and its own KTOS BSP
([`bsp/sam3x8e/`](../../bsp/sam3x8e/)).  The Cortex-M3 context-switch
assembly is identical to `bsp/stm32f103/`; the only differences are
the system tick source (TC0 instead of SysTick) and the peripheral
register set.

---

## What's in each board folder

Each board folder contains a board-specific README plus per-peripheral
examples.  The five canonical examples are:

| Example          | Demonstrates                                                       |
|------------------|--------------------------------------------------------------------|
| `adc`            | Single KTOS task, 1 Hz timer wake — `return 1000;`                  |
| `i2c`            | Single KTOS task, 5 s timer wake doing TWI bus scan                 |
| `led_control`    | **Two tasks**, `ktos_SendMsg(MSG_SET_MODE)` from UART to LED        |
| `button_control` | **Two tasks**, debouncer publishes `MSG_BUTTON_EVENT` to a UI task  |
| `uart`           | **Two tasks**, line-assembler hands lines to a command processor    |

The Nano folder has all five.  Uno and Pro Mini will get the same
five examples — most files will be copy-and-tweak from the Nano
versions because the MCU is identical.

---

## Common skeleton

Every example in every board folder follows the same shape:

```c
#include "../../../../../core/ktos.h"
#include <avr/io.h>
#include <avr/interrupt.h>

/* Bare-metal peripheral driver(s) ----------------------------------- */
static void uart_init(void)   { /* UBRR0H/L, UCSR0A/B/C */ }
static void uart_putc(char c) { while (!(UCSR0A & (1<<UDRE0))); UDR0 = c; }

/* KTOS platform callbacks ------------------------------------------- */
__attribute__((noreturn))
void ktos_Emergency(const char *msg) { uart_puts("[FATAL] "); uart_puts(msg); while (1); }
void ktos_DebugPrintf(const char *f, ...) { (void)f; }
void ktos_InitSys(void) { }

/* KTOS 1 ms tick — Timer1 CTC, set up by the AVR BSP ---------------- */
ISR(TIMER1_COMPA_vect) { ktos_timer_irq_handler(); }

/* Your task(s) ------------------------------------------------------ */
static WORD my_task(WORD MsgType, WORD sParam, LONG lParam) {
    switch (MsgType) {
        case KTOS_MSG_TYPE_INIT:  break;
        case KTOS_MSG_TYPE_TIMER: break;
    }
    return 1000;
}

/* main() — register tasks, give the CPU to KTOS forever ------------- */
int main(void) {
    uart_init();
    ktos_InitTask(my_task, 96, 4, 'T');
    ktos_RunOS();
    return 0;
}
```

---

## How to add support for a new Arduino board

1. Copy the closest existing folder (`Nano/` is the canonical
   starting point for any ATmega328P-based board).
2. Update each `platformio.ini` `board = …` line to the new
   PlatformIO board ID (`uno`, `pro16MHzatmega328`, …).
3. Adjust `upload_protocol` / `board_upload.speed` if the bootloader
   differs (Pro Mini at 3.3 V / 8 MHz wants `board_upload.speed = 57600`).
4. Re-verify peripheral pin assignments — the ATmega328P pin map is
   identical across these three boards, but other AVR boards (Mega,
   Leonardo) need real changes.
5. Add a row to the **Supported Boards** table above.

The KTOS scheduler and BSP do not change between boards in the same
MCU family.
