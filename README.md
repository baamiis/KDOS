# KTOS — Tiny Cooperative Task Switcher

[![CI Status](https://github.com/baamiis/KTOS/actions/workflows/ci.yml/badge.svg)](https://github.com/baamiis/KTOS/actions/workflows/ci.yml)
[![License: GPL v3](https://img.shields.io/badge/license-GPL--3.0--only-blue)](LICENSE)
[![Contributors](https://img.shields.io/github/contributors/baamiis/KTOS)](https://github.com/baamiis/KTOS/graphs/contributors)
[![Documentation](https://img.shields.io/badge/docs-ktos.co.uk-blue)](https://www.ktos.co.uk)

Main website: https://www.ktos.co.uk/

## What is KTOS?

KTOS is a simple cooperative task switcher for resource-constrained embedded systems. It allows different parts of a program to operate largely independently, with a footprint of around 4KB — making it an ideal alternative to heavier RTOS solutions like FreeRTOS or Zephyr on small microcontrollers.

## When should you use KTOS?

KTOS is the right choice when:
- Your MCU has **less than 32KB RAM** or **less than 256KB Flash**
- Your application uses **simple cooperative tasks** (no complex scheduling needed)
- A full RTOS is overkill for your use case

Rule of thumb: *"If your application logic + KTOS fits in under 32KB RAM, KTOS is the right choice."*

## Supported MCUs

KTOS provides ready-to-use Board Support Packages (BSPs) for the following microcontrollers:

| MCU | Core | RAM | Flash | BSP | Examples |
|-----|------|-----|-------|-----|----------|
| ATmega328P | AVR 8-bit | 2KB | 32KB | ✅ | 6 (Nano / Uno / Pro Mini) |
| ATmega2560 | AVR 8-bit | 8KB | 256KB | ✅ | 6 (Arduino Mega 2560) |
| ATtiny85 | AVR 8-bit | 512B | 8KB | ✅ | hello_ktos |
| MSP430G2553 | MSP430 16-bit | 512B | 16KB | ✅ | hello_ktos |
| STM32F030 | Cortex-M0 | 8KB | 64KB | ✅ | 6 (Nucleo-F030R8) |
| STM32L031 | Cortex-M0+ | 8KB | 32KB | ✅ | hello_ktos |
| STM32F103 | Cortex-M3 | 20KB | 64KB | ✅ | 6 (Blue Pill) |
| SAM3X8E | Cortex-M3 | 96KB | 512KB | ✅ | 6 (Arduino Due) |
| ESP8266 (LX106) | Xtensa 32-bit | ~30KB free | 1MB+ | ✅ | 6+ |
| PIC18F4550 | PIC18 8-bit | 2KB | 32KB | ⚠️ Experimental | hello_ktos |

**Compatible boards per MCU:**
- **ATmega328P**: Arduino Uno, Arduino Nano, Arduino Pro Mini
- **ATmega2560**: Arduino Mega 2560
- **ATtiny85**: Digispark
- **MSP430G2553**: MSP430 LaunchPad (MSP-EXP430G2)
- **STM32F030**: Nucleo-F030R8
- **STM32L031**: Nucleo-L031K6
- **STM32F103**: Blue Pill, Nucleo-F103RB, Maple Mini
- **SAM3X8E**: Arduino Due
- **ESP8266**: NodeMCU, Wemos D1 Mini, ESP-01
- **PIC18F4550**: Custom board (SDCC toolchain)

## Get Started

Visit **[ktos.co.uk](https://ktos.co.uk)** to:
- Select your dev board or bare MCU
- Download a ready-to-use KTOS firmware package
- Follow the wiring guide and datasheet notes (for bare MCU users)

Or build manually — see below.

## How does it work?

Each task has its own stack and message queue. Tasks communicate via simple messages and are executed on a round-robin basis when a message is available or their timer has expired. Once a task gets control it will not be pre-empted — no semaphores or mutexes needed for shared global variables.

Each task has an associated timer with a resolution of **1ms** and a maximum sleep of **65 seconds**.

### Task function

```c
WORD my_task(WORD MsgType, WORD sParam, LONG lParam)
{
    switch (MsgType) {
        case KTOS_MSG_TYPE_INIT:
            /* initialise hardware */
            break;
        case KTOS_MSG_TYPE_TIMER:
            /* periodic work */
            break;
    }
    return 500; /* sleep 500ms */
}
```

The task function takes 3 parameters:
- `MsgType` — 16-bit message type (`KTOS_MSG_TYPE_INIT`, `KTOS_MSG_TYPE_TIMER`, or user-defined)
- `sParam` — user-defined 16-bit value
- `lParam` — user-defined 32-bit value

The return value is the sleep duration in milliseconds (`MSG_WAIT` to sleep until explicitly woken).

## Repository Structure

```
KTOS/
├── core/               # Platform-independent KTOS kernel
│   ├── ktos.c          # Scheduler, task management
│   ├── ktos.h          # Public API
│   ├── ktos_hal.h      # Hardware abstraction interface
│   ├── ktos_multi.c    # Utilities and type definitions
│   └── ktos_multi.h
├── bsp/                # Board Support Packages (one per MCU)
│   ├── atmega328p/     # AVR ATmega328P BSP + Makefile
│   ├── stm32f103/      # STM32F103 Cortex-M3 BSP + Makefile
│   ├── stm32f030/      # STM32F030 Cortex-M0 BSP + Makefile
│   └── esp8266/        # ESP8266 Xtensa BSP + Makefile
├── examples/              # Ready-to-flash KTOS demos
│   ├── Arduino/           # ATmega328P boards (PlatformIO, bare-metal)
│   │   ├── Nano/          # 5 examples (adc, i2c, led_control, button_control, uart)
│   │   ├── Uno/           # 2 Uno-specific (usb_serial, eeprom) — Nano examples portable
│   │   └── Pro_Mini/      # 5 examples (same source as Nano, programmed via USB-TTL)
│   └── esp8266/           # Xtensa LX106 examples
├── scripts/
│   ├── build_all.sh    # Local build verification script
│   ├── package.py      # ZIP package generator for website
│   └── ktos_config.py  # BSP skeleton generator
└── .github/
    └── workflows/
        ├── ci.yml      # CI: builds all BSPs on every PR
        └── package.yml # Generates ZIP packages on merge to main
```

## Examples

Each `examples/<family>/` directory has a top-level README that walks
you through download, build, and flash.  Quick links:

| Family                          | Where                          | Highlights                                                        |
|---------------------------------|--------------------------------|-------------------------------------------------------------------|
| Arduino (ATmega328P)            | `examples/Arduino/`            | Bare-metal — Nano, Uno, Pro Mini. No Arduino framework.           |
| Arduino Mega (ATmega2560)       | `examples/Arduino/Mega/`       | 6 examples: uart, led, button, adc, i2c, spi. Direct registers.  |
| Arduino Due (SAM3X8E)           | `examples/Arduino/Due/`        | Cortex-M3 bare-metal, 96KB RAM.                                   |
| STM32F103 Blue Pill             | `examples/stm32f103/`          | 6 examples, 72 MHz, no HAL. PlatformIO + custom linker script.    |
| STM32F030 Nucleo-F030R8         | `examples/stm32f030/`          | 6 examples, 48 MHz Cortex-M0. New-style ADC/I2C/USART registers. |
| ESP8266 (Xtensa LX106)          | `examples/esp8266/`            | KTOS bridging from the SDK's windowed ABI into CALL0.             |

## Building Locally

**Install toolchains:**
```bash
# ARM (STM32)
sudo apt-get install gcc-arm-none-eabi libnewlib-arm-none-eabi

# AVR (ATmega)
sudo apt-get install gcc-avr avr-libc
```

**Build all BSPs and verify:**
```bash
bash scripts/build_all.sh
```

**Build a specific BSP:**
```bash
make -C bsp/atmega328p
make -C bsp/stm32f103
make -C bsp/stm32f030
```

**Generate a firmware ZIP package:**
```bash
python scripts/package.py --mcu atmega328p --out packages/
python scripts/package.py --mcu all --out packages/
```

## Adding a new BSP

1. Create `bsp/<mcu_name>/ktos_bsp.c` implementing all 6 functions from `core/ktos_hal.h`
2. Create `bsp/<mcu_name>/Makefile` using the correct toolchain
3. Run `bash scripts/build_all.sh` to verify it builds
4. Open a PR — CI will build and verify automatically

The 6 HAL functions to implement:
```c
void  ktos_hal_DisableInterrupts(void);
void  ktos_hal_EnableInterrupts(void);
void *ktos_hal_InitTaskStack(...);
void  ktos_hal_ContextSwitch(void **current_sp, void *next_sp);
void  ktos_hal_StartScheduler(void *first_task_sp);
void  ktos_hal_InitSystemTimer(void (*isr)(void));
```

## CI / CD Pipeline

Every pull request automatically:
1. Builds all BSPs with their correct toolchain
2. Runs `cppcheck` static analysis on core and BSPs
3. Verifies documentation and license files
4. Blocks merge if any check fails

On merge to `main`, ZIP packages are generated and uploaded as GitHub artifacts.

## Contributing

We welcome contributions — especially new BSPs for unsupported MCUs!

Please see [CONTRIBUTING.md](CONTRIBUTING.md) for full guidelines.

- Found a bug? [Open an issue](https://github.com/baamiis/KTOS/issues/new?template=bug_report.md)
- Have an idea? [Request a feature](https://github.com/baamiis/KTOS/issues/new?template=feature_request.md)
- New to the project? Check out issues labeled [`good-first-issue`](https://github.com/baamiis/KTOS/labels/good-first-issue)

Please read our [Code of Conduct](CODE_OF_CONDUCT.md) before contributing.

## Licensing

KTOS is dual-licensed by **Khalid Hamdou / BAAMIIS LIMITED**, the original
author and sole copyright holder.

---

### Open Source — GPL v3

If you are building an **open source project**, KTOS is free to use under the
[GNU General Public License v3](LICENSE).

Under GPL v3 you must:
- Release your source code under a GPL v3 compatible license
- Retain all copyright notices and the NOTICE file
- State any significant changes you make to KTOS

---

### Commercial License

If you are building a **proprietary or closed-source product** and cannot
comply with the GPL v3, you must purchase a Commercial License from
BAAMIIS LIMITED.

The Commercial License allows you to:
- Use KTOS in closed-source products
- Distribute without disclosing your source code
- Receive direct support from Khalid Hamdou

**Contact for commercial licensing:**

|-------- |---------------------------------|
| Author  | Khalid Hamdou                   |
| Company | BAAMIIS LIMITED                 |
| Email   | baamiis7@gmail.com               |
| GitHub  | https://github.com/baamiis/KTOS |

See [COMMERCIAL_LICENSE](COMMERCIAL_LICENSE) for full terms.

---

> KTOS is the original work of Khalid Hamdou / BAAMIIS LIMITED.
> No person or organisation may claim authorship or ownership of this software.