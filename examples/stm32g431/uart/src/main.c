#include <stdint.h>
#include "ktos.h"

/* USART2 on PA2/PA3, default clock HSI 16 MHz */
#define RCC_AHB2ENR   (*(volatile uint32_t*)0x4002104C)
#define RCC_APB1ENR1  (*(volatile uint32_t*)0x40021058)
#define GPIOA_MODER   (*(volatile uint32_t*)0x48000000)
#define GPIOA_AFRL    (*(volatile uint32_t*)0x48000020)
#define USART2_CR1    (*(volatile uint32_t*)0x40004400)
#define USART2_BRR    (*(volatile uint32_t*)0x4000440C)
#define USART2_TDR    (*(volatile uint32_t*)0x40004428)
#define USART2_ISR    (*(volatile uint32_t*)0x4000441C)

static void uart_init(void) {
    RCC_AHB2ENR  |= (1u << 0);
    RCC_APB1ENR1 |= (1u << 17);
    GPIOA_MODER   = (GPIOA_MODER & ~(0xFu << 4)) | (0xAu << 4);
    GPIOA_AFRL    = (GPIOA_AFRL  & ~(0xFFu << 8)) | (0x77u << 8);
    USART2_BRR    = 16000000UL / 115200UL;
    USART2_CR1    = (1u << 3) | (1u << 0);
}

static void uart_putc(char c) {
    while (!(USART2_ISR & (1u << 7)));
    USART2_TDR = (uint8_t)c;
}

static void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

#define STACK_SIZE 128
static uint32_t uart_stack[STACK_SIZE];

static WORD uart_task(WORD msg, WORD p1, LONG p2) {
    (void)p1; (void)p2;
    if (msg == KTOS_MSG_TYPE_INIT) {
        uart_init();
        uart_puts("KTOS STM32G431 UART ready\r\n");
    } else {
        uart_puts("hello from KTOS\r\n");
    }
    return 1000;
}

static KTOS_TASK tasks[1];

int main(void) {
    KTOS_Init(tasks, 1);
    KTOS_CreateTask(uart_task, uart_stack, sizeof(uart_stack));
    KTOS_Start();
    for (;;);
}
