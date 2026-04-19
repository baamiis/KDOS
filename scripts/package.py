"""
KTOS firmware package generator.
Creates a ZIP for a given MCU containing all files the engineer needs.
Usage: python scripts/package.py --mcu atmega328p --out packages/
"""

import os
import shutil
import zipfile
import argparse

CORE_FILES = [
    "core/ktos.c",
    "core/ktos.h",
    "core/ktos_hal.h",
    "core/ktos_multi.c",
    "core/ktos_multi.h",
]

BSP_DIR = "bsp"

MCU_INFO = {
    "atmega328p": {
        "name": "ATmega328P",
        "boards": ["Arduino Uno", "Arduino Nano", "Arduino Pro Mini"],
        "toolchain": "avr-gcc",
    },
    "stm32f103": {
        "name": "STM32F103",
        "boards": ["Blue Pill", "Nucleo-F103RB", "Maple Mini"],
        "toolchain": "arm-none-eabi-gcc",
    },
    "stm32f030": {
        "name": "STM32F030",
        "boards": ["Nucleo-F030R8"],
        "toolchain": "arm-none-eabi-gcc",
    },
    "esp8266": {
        "name": "ESP8266 (LX106)",
        "boards": ["NodeMCU", "Wemos D1 Mini", "ESP-01"],
        "toolchain": "xtensa-lx106-elf-gcc",
    },
}


def get_supported_mcus():
    return [d for d in os.listdir(BSP_DIR)
            if os.path.isdir(os.path.join(BSP_DIR, d))]


def generate_readme(mcu):
    info = MCU_INFO.get(mcu, {"name": mcu, "boards": [], "toolchain": "unknown"})
    boards_str = ", ".join(info["boards"]) if info["boards"] else "N/A"
    return f"""# KTOS for {info['name']}

## Compatible boards
{boards_str}

## Toolchain
{info['toolchain']}

## Files
- ktos.c / ktos.h         — KTOS core scheduler
- ktos_multi.c / .h       — utilities and type definitions
- ktos_hal.h              — HAL interface
- ktos_bsp.c              — BSP implementation for {info['name']}
- Makefile                — build configuration
- main_example.c          — example starter application

## Build
    make

## Getting started
1. Copy these files into your project
2. Implement your tasks in main_example.c
3. Build with make or your IDE
4. Flash to your device

## Documentation
https://ktos.co.uk
"""


def generate_example_main(mcu):
    return f"""/*
 * KTOS example starter for {MCU_INFO.get(mcu, {{}}).get('name', mcu)}
 * Edit this file to add your application tasks.
 */
#include "ktos.h"
#include "ktos_multi.h"

static struct ktos_TASK *TaskMain;

static WORD ktos_TaskMain(WORD MsgType, WORD sParam, LONG lParam)
{{
    (void)sParam;
    (void)lParam;

    switch (MsgType) {{
        case KTOS_MSG_TYPE_INIT:
            /* Initialise your hardware here */
            ktos_InitSys();
            break;

        case KTOS_MSG_TYPE_TIMER:
            /* Called every time the task timer fires */
            break;

        default:
            break;
    }}

    return 500; /* Sleep 500ms then wake up */
}}

int main(void)
{{
    TaskMain = ktos_InitTask(ktos_TaskMain,
                             TASK_MAIN_STACK_SIZE,
                             TASK_MAIN_QUEUE_SIZE,
                             TASK_MAIN_ID);

    ktos_SendMsg(TaskMain, KTOS_MSG_TYPE_INIT, 0, 0);
    ktos_RunOS();
    return 0;
}}
"""


def create_package(mcu, output_dir):
    mcu_bsp_dir = os.path.join(BSP_DIR, mcu)
    if not os.path.isdir(mcu_bsp_dir):
        print(f"Error: No BSP found for '{mcu}' at {mcu_bsp_dir}")
        return False

    os.makedirs(output_dir, exist_ok=True)
    zip_path = os.path.join(output_dir, f"ktos_{mcu}.zip")

    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        # Core files
        for f in CORE_FILES:
            if os.path.exists(f):
                zf.write(f, os.path.basename(f))

        # BSP files
        for fname in os.listdir(mcu_bsp_dir):
            fpath = os.path.join(mcu_bsp_dir, fname)
            if os.path.isfile(fpath):
                zf.write(fpath, fname)

        # Generated files
        zf.writestr("README.md", generate_readme(mcu))
        zf.writestr("main_example.c", generate_example_main(mcu))

    print(f"Created: {zip_path}")
    return True


def main():
    parser = argparse.ArgumentParser(description="KTOS package generator")
    parser.add_argument("--mcu", help="MCU name (e.g. atmega328p). Use 'all' for all.")
    parser.add_argument("--out", default="packages", help="Output directory")
    parser.add_argument("--list", action="store_true", help="List supported MCUs")
    args = parser.parse_args()

    if args.list:
        print("Supported MCUs:")
        for mcu in get_supported_mcus():
            info = MCU_INFO.get(mcu, {})
            print(f"  {mcu:<20} {info.get('name', '')}")
        return

    if not args.mcu:
        parser.print_help()
        return

    if args.mcu == "all":
        for mcu in get_supported_mcus():
            create_package(mcu, args.out)
    else:
        create_package(args.mcu, args.out)


if __name__ == "__main__":
    main()
