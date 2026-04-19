# Contributing to KTOS

Thank you for your interest in contributing to KTOS! This document provides guidelines and instructions for contributing to our tiny cooperative RTOS alternative.

## Table of Contents
- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [Development Workflow](#development-workflow)
- [Adding a New BSP](#adding-a-new-bsp)
- [Coding Standards](#coding-standards)
- [Commit Guidelines](#commit-guidelines)
- [Pull Request Process](#pull-request-process)
- [Testing](#testing)

## Code of Conduct

This project adheres to a Code of Conduct. By participating, you are expected to uphold this code. Please report unacceptable behavior by opening an issue.

## Getting Started

### Prerequisites

- Git
- Toolchain for your target platform:
  - **ARM (STM32)**: `sudo apt-get install gcc-arm-none-eabi libnewlib-arm-none-eabi`
  - **AVR (ATmega/ATtiny)**: `sudo apt-get install gcc-avr avr-libc`
  - **Xtensa (ESP8266)**: Install the ESP8266 SDK toolchain
- Python 3 (for packaging scripts)
- cppcheck (for static analysis): `sudo apt-get install cppcheck`

### Setting Up Your Development Environment

1. **Fork the repository** on GitHub, then clone your fork:
   ```bash
   git clone https://github.com/YOUR-USERNAME/KTOS.git
   cd KTOS
   git remote add upstream https://github.com/baamiis/KTOS.git
   ```

2. **Install toolchains** (see Prerequisites above)

3. **Verify everything builds:**
   ```bash
   bash scripts/build_all.sh
   ```
   All BSPs must pass before you start making changes.

4. **The pre-push hook is automatic** — `build_all.sh` runs before every `git push`, blocking the push if any BSP fails to build.

## Development Workflow

1. **Create a branch:**
   ```bash
   git checkout -b feature/your-feature-name
   ```
   Branch naming:
   - `feature/description` — new features
   - `fix/description` — bug fixes
   - `bsp/mcu-name` — new BSP additions
   - `docs/description` — documentation only

2. **Make your changes** following the coding standards below.

3. **Verify builds locally before pushing:**
   ```bash
   bash scripts/build_all.sh
   ```

4. **Commit and push** — the pre-push hook will run `build_all.sh` automatically:
   ```bash
   git push origin feature/your-feature-name
   ```

5. **Open a Pull Request** on GitHub — CI will build all BSPs and run static analysis automatically. All checks must be green before a maintainer can merge.

## Adding a New BSP

This is the most valuable contribution you can make — each new BSP means a new MCU is supported on [ktos.co.uk](https://ktos.co.uk).

### Steps

1. **Create the BSP directory:**
   ```bash
   mkdir bsp/<mcu_name>
   ```

2. **Implement `ktos_bsp.c`** — all 6 HAL functions from `core/ktos_hal.h`:

   ```c
   #include "../../core/ktos_hal.h"

   void ktos_hal_DisableInterrupts(void) { /* disable interrupts */ }
   void ktos_hal_EnableInterrupts(void)  { /* enable interrupts  */ }

   void *ktos_hal_InitTaskStack(void *p_stack_base,
                                 unsigned int stack_size_bytes,
                                 WORD (*task_func_addr)(WORD, WORD, LONG),
                                 void (*task_exit_handler_addr)(WORD),
                                 WORD initial_msg_type,
                                 WORD initial_sparam,
                                 LONG initial_lparam) { /* setup stack */ }

   void ktos_hal_ContextSwitch(void **p_current_sp, void *next_sp) { /* assembly */ }
   void ktos_hal_StartScheduler(void *first_task_sp)               { /* assembly */ }
   void ktos_hal_InitSystemTimer(void (*isr)(void))                 { /* 1ms timer */ }
   ```

3. **Create a `Makefile`** using the correct toolchain for that MCU. Use an existing BSP Makefile as a template (e.g. `bsp/stm32f103/Makefile` for ARM, `bsp/atmega328p/Makefile` for AVR).

4. **Verify it builds:**
   ```bash
   bash scripts/build_all.sh
   ```

5. **Add MCU info to `scripts/package.py`** in the `MCU_INFO` dictionary so the website can generate ZIP packages for it.

6. **Open a PR** — describe which MCU you added, which boards it supports, and which hardware you tested on if possible.

### BSP implementation tips

- **Context switch** is the hardest part — it requires architecture-specific assembly. Study existing BSPs for examples.
- **Cortex-M3** (STM32F103): use `push {r4-r11}` / `str sp, [r0]` / `mov sp, r1` / `pop {r4-r11}`
- **Cortex-M0** (STM32F030): same idea but SP cannot be used directly with STR — use `mov r2, sp` then `str r2, [r0]`
- **AVR**: push/pop all 32 registers + SREG, use `in`/`out` for SP
- Always use `--specs=nosys.specs` in ARM linker flags for bare-metal builds

## Coding Standards

### C Code Style

- **Indentation**: 4 spaces (no tabs)
- **Line length**: Maximum 100 characters
- **Naming conventions**:
  - Functions: `ktos_function_name` prefix for all public functions
  - HAL functions: `ktos_hal_function_name` prefix
  - Types/structs: `ktos_TypeName` (e.g. `ktos_TASK`, `ktos_MSG`)
  - Enum values: `KTOS_ENUM_VALUE` (e.g. `KTOS_MSG_TYPE_INIT`)
  - Macros/constants: `UPPER_CASE`
- **No dynamic allocation** in core KTOS code
- Pass `cppcheck` with no warnings before submitting

### Commit Guidelines

Use conventional commit format:

```
<type>: <short description>

<optional body>
```

Types: `feat`, `fix`, `bsp`, `docs`, `style`, `refactor`, `chore`

Examples:
```
bsp: add ATmega2560 support

feat: add message queue overflow callback

fix: correct Cortex-M0 context switch SP constraint
```

## Pull Request Process

1. All CI checks must pass (build, cppcheck, docs, license)
2. At least 1 approving review from a maintainer is required
3. All review comments must be resolved
4. Maintainers will merge — contributors cannot merge directly to `main`

## Testing

- Run `bash scripts/build_all.sh` — all BSPs must pass
- If adding a new BSP, test on real hardware where possible and report results in the PR
- Run `cppcheck` on your files:
  ```bash
  cppcheck --enable=warning,style --suppress=missingIncludeSystem --error-exitcode=1 bsp/<mcu_name>/
  ```

Thank you for contributing to KTOS!
