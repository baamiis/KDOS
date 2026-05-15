# KTOS ROS 2 Serial Bridge — Firmware

This directory contains the KTOS firmware side of the ROS 2 Serial Bridge demo.

## File overview

| File            | Purpose                                                  |
|-----------------|----------------------------------------------------------|
| `main.c`        | Entry point — registers tasks, calls `ktos_RunOS()`      |
| `ktos_app.c/h`  | Five KTOS tasks: heartbeat, sensor, status, RX, command  |
| `ktos_serial.c/h` | Lightweight line-buffer serial helper                  |
| `board_hal.c/h` | Board HAL stubs — replace with real MCU peripheral code  |

## Building (POSIX simulation)

```bash
gcc -std=c99 -Wall -Wextra \
    -I../../../core \
    main.c ktos_app.c ktos_serial.c board_hal.c \
    ../../../core/ktos.c \
    -o ktos_bridge_sim
./ktos_bridge_sim
```

> You will need to provide a BSP implementation for your target.
> The POSIX stub in `board_hal.c` lets you compile and test logic on a PC.

## Porting to a real board

1. **Copy** `board_hal.c` to your board-specific directory.
2. **Search for `TODO`** comments — each marks one peripheral to implement.
3. **Wire the 1 ms tick** that drives KTOS to the same counter returned by
   `board_millis()`.
4. Replace `board_serial_read_char()` with a non-blocking UART RX buffer read.
5. Replace `board_serial_write()` with a non-blocking UART TX (DMA or ring buffer).

### STM32 quick-start

```c
/* board_hal.c — STM32 snippets */
uint32_t board_millis(void)      { return HAL_GetTick(); }
void     board_led_set(int s)    { HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, s); }
void     board_serial_write(const char *t) {
    HAL_UART_Transmit(&huart1, (uint8_t *)t, strlen(t), 100);
}
int board_serial_read_char(char *c) {
    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE)) {
        *c = (char)huart1.Instance->DR; return 1;
    }
    return 0;
}
```

### KTOS timer ISR wiring (STM32)

```c
void SysTick_Handler(void) {
    HAL_IncTick();
    ktos_timer_irq_handler();   /* drives KTOS task timers */
}
```

## Serial protocol

See `../protocol/ktos_serial_protocol.md` for the full message reference.
