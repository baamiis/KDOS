/*
 * KTOS — Tiny Cooperative Task Switcher
 * Copyright (C) 2004-2025 Khalid Hamdou / BAAMIIS LIMITED
 * All rights reserved.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Contact: baamiis7@gmail.com
 */

/**
 * @file board_hal.c
 * @brief Stub Board HAL — POSIX simulation (printf / stdin).
 *
 * This file compiles on any host with a C99 compiler and lets you test the
 * firmware logic on a PC before flashing to real hardware.
 *
 * To port to a real board:
 *  1. Replace each TODO section with MCU-specific register writes.
 *  2. Keep board_hal.h unchanged — the firmware tasks use only the API.
 *
 * Porting examples are shown as comments inside each function.
 */

#include "board_hal.h"
#include <stdio.h>
#include <time.h>

/* =========================================================================
 * Internal simulation state
 * ========================================================================= */

static int  g_led_state   = 0;
static int  g_motor_speed = 0;

/* =========================================================================
 * Time
 * ========================================================================= */

uint32_t board_millis(void)
{
    /*
     * POSIX stub: use clock_gettime for a monotonic millisecond counter.
     *
     * TODO STM32:   return HAL_GetTick();
     * TODO AVR:     return timer1_ms_count;   // incremented in ISR
     * TODO ESP32:   return (uint32_t)(esp_timer_get_time() / 1000ULL);
     */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000UL + ts.tv_nsec / 1000000UL);
}

/* =========================================================================
 * LED
 * ========================================================================= */

void board_led_set(int state)
{
    g_led_state = state ? 1 : 0;

    /*
     * TODO STM32:   HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin,
     *                   state ? GPIO_PIN_SET : GPIO_PIN_RESET);
     * TODO AVR:     if (state) PORTB |= _BV(PB5); else PORTB &= ~_BV(PB5);
     * TODO ESP32:   gpio_set_level(LED_GPIO, state);
     */
    printf("[HAL] LED %s\n", g_led_state ? "ON" : "OFF");
}

int board_led_get(void)
{
    return g_led_state;
}

/* =========================================================================
 * Motor
 * ========================================================================= */

void board_motor_set(int speed)
{
    /* Clamp to [-100, 100] */
    if (speed >  100) speed =  100;
    if (speed < -100) speed = -100;
    g_motor_speed = speed;

    /*
     * TODO STM32:   Set PWM duty cycle on TIM3_CH1 and direction GPIO.
     * TODO AVR:     analogWrite(MOTOR_PWM_PIN, abs(speed) * 255 / 100);
     *               digitalWrite(MOTOR_DIR_PIN, speed >= 0 ? HIGH : LOW);
     * TODO ESP32:   mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0,
     *                   MCPWM_OPR_A, (float)abs(speed));
     */
    printf("[HAL] MOTOR speed=%d\n", g_motor_speed);
}

int board_motor_get(void)
{
    return g_motor_speed;
}

/* =========================================================================
 * Serial I/O
 * ========================================================================= */

void board_serial_write(const char *text)
{
    /*
     * TODO STM32:   HAL_UART_Transmit(&huart1, (uint8_t *)text,
     *                   strlen(text), HAL_MAX_DELAY);
     *               (Better: DMA or TX ring buffer to avoid blocking.)
     * TODO AVR:     uart_puts(text);
     * TODO ESP32:   uart_write_bytes(UART_NUM_0, text, strlen(text));
     */
    fputs(text, stdout);
    fflush(stdout);
}

int board_serial_read_char(char *c)
{
    /*
     * Non-blocking read from RX buffer.
     * Return 1 if a byte was available, 0 if not.
     *
     * TODO STM32:   if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE)) {
     *                   *c = (char)huart1.Instance->DR; return 1; }
     *               return 0;
     * TODO AVR:     if (UCSR0A & _BV(RXC0)) { *c = UDR0; return 1; }
     *               return 0;
     * TODO ESP32:   return uart_read_bytes(UART_NUM_0, (uint8_t *)c, 1, 0);
     *
     * POSIX stub: non-blocking stdin would need termios raw mode.
     * For now, always return 0 (no data) so the firmware loop runs cleanly.
     * On a real board this will be replaced by a UART RX register check.
     */
    (void)c;
    return 0;
}

/* =========================================================================
 * System initialisation
 * ========================================================================= */

void board_init(void)
{
    /*
     * TODO STM32:   SystemClock_Config(); MX_GPIO_Init(); MX_USART1_UART_Init();
     * TODO AVR:     uart_init(115200); timer1_init_1ms(); sei();
     * TODO ESP32:   uart_config_t cfg = { .baud_rate = 115200, ... };
     *               uart_param_config(UART_NUM_0, &cfg);
     */
    printf("[HAL] board_init() — POSIX simulation\n");
}
