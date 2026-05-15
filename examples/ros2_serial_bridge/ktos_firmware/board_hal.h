/*
 * KTOS — Tiny Cooperative Task Switcher
 * Copyright (C) 2004-2025 Khalid Hamdou / BAAMIIS LIMITED
 * All rights reserved.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Contact: baamiis7@gmail.com
 */

/**
 * @file board_hal.h
 * @brief Board Hardware Abstraction Layer for the KTOS ROS 2 Serial Bridge demo.
 *
 * Implement every function declared here in board_hal.c for your target board.
 * The firmware compiles and runs as a simulation when using the stub
 * implementation in board_hal.c — replace stubs with real hardware calls.
 *
 * Supported targets (add your own):
 *   STM32F103  — UART1, SysTick, GPIO
 *   ATmega328P — USART0, Timer1, GPIO
 *   ESP32      — UART0, FreeRTOS tick, GPIO
 *   POSIX sim  — printf + stdin (default stubs)
 */

#ifndef BOARD_HAL_H
#define BOARD_HAL_H

#include <stdint.h>

/* =========================================================================
 * Time
 * ========================================================================= */

/**
 * @brief Return milliseconds elapsed since boot.
 *
 * On a bare-metal MCU, source this from the same 1 ms tick that drives the
 * KTOS timer ISR (SysTick, Timer1, FRC1, etc.).
 *
 * TODO: Replace stub with real hardware timer read.
 */
uint32_t board_millis(void);

/* =========================================================================
 * LED
 * ========================================================================= */

/**
 * @brief Set the onboard LED.
 * @param state  1 = on, 0 = off
 *
 * TODO: Drive a real GPIO pin.
 */
void board_led_set(int state);

/* =========================================================================
 * Motor
 * ========================================================================= */

/**
 * @brief Set motor speed.
 * @param speed  Signed percentage: -100 (full reverse) to +100 (full forward).
 *               0 = stop.
 *
 * TODO: Write PWM duty cycle / direction pin for your motor driver.
 */
void board_motor_set(int speed);

/* =========================================================================
 * Serial I/O
 * ========================================================================= */

/**
 * @brief Write a null-terminated string to the serial port (UART/USB CDC).
 *
 * Must be non-blocking or fast enough not to stall the cooperative scheduler.
 * On STM32 use DMA or a TX ring buffer; on AVR use a TX interrupt buffer.
 *
 * TODO: Replace stub with real UART transmit.
 */
void board_serial_write(const char *text);

/**
 * @brief Read one character from the serial RX buffer (non-blocking).
 * @param c  Output: received character.
 * @return   1 if a character was available, 0 if the buffer was empty.
 *
 * TODO: Replace stub with real UART receive.
 */
int board_serial_read_char(char *c);

/* =========================================================================
 * System initialisation
 * ========================================================================= */

/**
 * @brief Initialise clocks, UART, GPIO, and any other peripherals.
 *
 * Called once from main() before ktos_RunOS().
 *
 * TODO: Add MCU-specific peripheral init.
 */
void board_init(void);

#endif /* BOARD_HAL_H */
