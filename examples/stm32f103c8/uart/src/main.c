#include <stdint.h>
#include "ktos.h"

#define RCC_APB2ENR  (*((volatile uint32_t *)0x40021018))
#define GPIOA_CRH    (*((volatile uint32_t *)0x40010804))
#define USART1_SR    (*((volatile uint32_t *)0x40013800))
#define USART1_DR    (*((volatile uint32_t *)0x40013804))
#define USART1_BRR   (*((volatile uint32_t *)0x40013808))
#define USART1_CR1   (*((volatile uint32_t *)0x4001380C))

#define STACK_SIZE 128
static uint32_t tx_stack[STACK_SIZE];
static uint32_t rx_stack[STACK_SIZE];

static void uart_init(void) {
    RCC_APB2ENR |= (1u << 14) | (1u << 2);
    GPIOA_CRH   &= ~(0xFFu << 4);
    GPIOA_CRH   |=  (0x4Bu << 4);
    USART1_BRR   = 0x1D4C;   /* 9600 baud @ 72 MHz: 72000000/9600 = 7500 = 0x1D4C */
    USART1_CR1   = (1u << 13) | (1u << 3) | (1u << 2);
}

static void uart_putc(char c) {
    while (!(USART1_SR & (1u << 7)));
    USART1_DR = (uint8_t)c;
}

static void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

static void tx_task(void) {
    for (;;) {
        uart_puts("KTOS UART TX task running\r\n");
        for (volatile int i = 0; i < 800000; i++);
        ktos_yield();
    }
}

static void rx_task(void) {
    for (;;) {
        if (USART1_SR & (1u << 5)) {
            char c = (char)(USART1_DR & 0xFF);
            uart_putc(c);
        }
        ktos_yield();
    }
}

int main(void) {
    uart_init();
    ktos_init();
    ktos_task_create(tx_task, tx_stack, STACK_SIZE);
    ktos_task_create(rx_task, rx_stack, STACK_SIZE);
    ktos_hal_InitSystemTimer();
    ktos_hal_StartScheduler();
    for (;;);
}
