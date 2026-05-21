/**
 * @file main.c
 * @brief KTOS "Hello from KTOS!" demo for ESP8266 (Xtensa LX106).
 *
 * Prints "Hello from KTOS!" to UART0 once per second.
 * Uses direct register access — no ESP8266 SDK dependency inside this file.
 *
 * Compile flag: -mlongcalls (toolchain defaults to CALL0 ABI; no -mabi=call0 needed)
 *
 * Expected serial output (74880 8N1):
 * @code
 * =============================
 *   KTOS on ESP8266 - running!
 * =============================
 * Hello from KTOS!
 * Hello from KTOS!
 * ...
 * @endcode
 */

#include "../../../core/ktos.h"
#include <stdint.h>

/* =========================================================================
 * UART0 — direct register access, 74880 8N1 at ~52 MHz
 * ========================================================================= */

#define UART0_FIFO    (*(volatile uint32_t *)0x60000000UL)
#define UART0_STATUS  (*(volatile uint32_t *)0x6000001CUL)
#define UART0_CONF0   (*(volatile uint32_t *)0x60000020UL)
#define UART0_CLKDIV  (*(volatile uint32_t *)0x60000014UL)

/* 74880 baud: 52 000 000 / 74880 ≈ 694 (ROM PLL sets ~52 MHz before user code) */
#define UART_BAUD_DIV 694u

static void uart_init(void)
{
    UART0_CLKDIV = UART_BAUD_DIV;
    UART0_CONF0  = 0x1cUL;     /* 8 data bits, no parity, 1 stop bit */
}

static void uart_putc(char c)
{
    /* TX FIFO count is in STATUS[23:16]; capacity is 128 bytes */
    while (((UART0_STATUS >> 16) & 0xFFu) >= 126u)
        ;
    UART0_FIFO = (uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s) uart_putc(*s++);
}

/* =========================================================================
 * Delay — uses Xtensa CCOUNT register (increments every CPU cycle, ~52 MHz)
 * ========================================================================= */

#define CPU_MHZ 52UL

static void delay_ms(uint32_t ms)
{
    uint32_t t0, tn;
    __asm__ volatile ("rsr %0, CCOUNT" : "=r"(t0));
    do {
        __asm__ volatile ("rsr %0, CCOUNT" : "=r"(tn));
    } while (tn - t0 < ms * (CPU_MHZ * 1000UL));
}

/* =========================================================================
 * Required KTOS platform callbacks
 * ========================================================================= */

__attribute__((noreturn)) void ktos_Emergency(const char *msg)
{
    uart_puts("\r\n[KTOS FATAL] ");
    uart_puts(msg);
    uart_puts("\r\n");
    while (1);
}

void ktos_DebugPrintf(const char *fmt, ...) { (void)fmt; }
void ktos_InitSys(void) {}

/* =========================================================================
 * Hello task
 * ========================================================================= */

/**
 * @brief Prints "Hello from KTOS!" every second.
 *
 * This is a single-task demo.  The task deliberately loops internally
 * rather than returning a sleep value, so KTOS's cooperative context
 * switch path is not exercised (the full multi-task switch requires
 * ktos_SwitchTask to be running on the OS stack — a feature for a
 * future multi-task example).
 *
 * To add a second task later, replace the for(;;) loop with
 * @c return 1000 and see @c examples/esp8266.md for the multi-task notes.
 */
WORD hello_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam;
    (void)lParam;

    if (MsgType == KTOS_MSG_TYPE_INIT) {
        delay_ms(50);
        uart_puts("\033[2J\033[H");   /* clear screen, cursor home */
        uart_puts("=============================\r\n");
        uart_puts("  KTOS on ESP8266 - running!\r\n");
        uart_puts("=============================\r\n");
    }

    for (;;) {
        uart_puts("Hello from KTOS!\r\n");
        delay_ms(1000);
    }

    return KTOS_MSG_SLEEP_INDEFINITLY;    /* unreachable — suppresses compiler warning */
}

/* =========================================================================
 * CALL0 application entry — called from startup.c via callx0
 * ========================================================================= */

/**
 * @brief Application entry point (CALL0 ABI).
 *
 * startup.c's user_init() crosses into this function using the callx0
 * instruction so the stack pointer is preserved correctly across the
 * windowed → CALL0 ABI boundary.
 */
void ktos_app_main(void)
{
    /* Disable hardware watchdog — not fed in this bare-metal demo. */
    *(volatile uint32_t *)0x60000900UL = 0;

    uart_init();

    ktos_InitTask(hello_task,
                  TASK_MAIN_STACK_SIZE,
                  TASK_MAIN_QUEUE_SIZE,
                  'H');

    ktos_RunOS();   /* never returns */
}
