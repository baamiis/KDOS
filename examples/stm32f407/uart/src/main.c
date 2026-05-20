#include <stdint.h>
#include <string.h>
#include "ktos.h"

/* USART2: PA2=TX, PA3=RX  (APB1=42 MHz) */
#define RCC_AHB1ENR   (*(volatile uint32_t*)0x40023830)
#define RCC_APB1ENR   (*(volatile uint32_t*)0x40023840)
#define GPIOA_MODER   (*(volatile uint32_t*)0x40020000)
#define GPIOA_AFRL    (*(volatile uint32_t*)0x40020020)
#define USART2_SR     (*(volatile uint32_t*)0x40004400)
#define USART2_DR     (*(volatile uint32_t*)0x40004404)
#define USART2_BRR    (*(volatile uint32_t*)0x40004408)
#define USART2_CR1    (*(volatile uint32_t*)0x4000440C)

static void uart_init(void) {
    RCC_AHB1ENR |= (1U << 0);   /* GPIOA */
    RCC_APB1ENR |= (1U << 17);  /* USART2 */
    /* PA2, PA3 alternate function 7 */
    GPIOA_MODER &= ~(0xFU << 4);
    GPIOA_MODER |=  (0xAU << 4);
    GPIOA_AFRL  &= ~(0xFFU << 8);
    GPIOA_AFRL  |=  (0x77U << 8);
    /* 115200 @ 42 MHz: BRR = 42000000/115200 = 364 (approx) */
    USART2_BRR   = 364U;
    USART2_CR1   = (1U << 13) | (1U << 3) | (1U << 2); /* UE | TE | RE */
}

static void uart_putc(char c) {
    while (!(USART2_SR & (1U << 7)));
    USART2_DR = (uint8_t)c;
}

static void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

KTOS_STACK_DEF(task_tx_stack,  256);
KTOS_STACK_DEF(task_hb_stack,  256);

static volatile uint32_t tick_count = 0;

static void task_transmit(void) {
    while (1) {
        uart_puts("[KTOS] UART task running\r\n");
        ktos_yield();
    }
}

static void task_heartbeat(void) {
    char buf[32];
    while (1) {
        tick_count++;
        uart_puts("[KTOS] heartbeat\r\n");
        ktos_yield();
    }
}

int main(void) {
    uart_init();
    uart_puts("KTOS STM32F407 UART Example\r\n");
    ktos_init();
    ktos_task_create(task_transmit,  task_tx_stack, sizeof(task_tx_stack));
    ktos_task_create(task_heartbeat, task_hb_stack, sizeof(task_hb_stack));
    ktos_start();
    while (1);
}
