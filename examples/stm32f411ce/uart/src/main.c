#include <stdint.h>
#include <string.h>
#include "ktos.h"

#define RCC_AHB1ENR  (*((volatile uint32_t *)0x40023830))
#define RCC_APB2ENR  (*((volatile uint32_t *)0x40023844))
#define GPIOA_MODER  (*((volatile uint32_t *)0x40020000))
#define GPIOA_AFRH   (*((volatile uint32_t *)0x40020024))
#define USART1_SR    (*((volatile uint32_t *)0x40011000))
#define USART1_DR    (*((volatile uint32_t *)0x40011004))
#define USART1_BRR   (*((volatile uint32_t *)0x40011008))
#define USART1_CR1   (*((volatile uint32_t *)0x4001100C))

#define STACK_SIZE   256

static uint32_t uart_stack[STACK_SIZE];
static uint32_t idle_stack[STACK_SIZE];
static ktos_tcb_t uart_tcb;
static ktos_tcb_t idle_tcb;

static void uart_init(void) {
    RCC_AHB1ENR |= (1 << 0);
    RCC_APB2ENR |= (1 << 4);
    GPIOA_MODER &= ~(0xF << 18);
    GPIOA_MODER |=  (0xA << 18);
    GPIOA_AFRH  &= ~(0xFF << 4);
    GPIOA_AFRH  |=  (0x77 << 4);
    USART1_BRR   = 0x683;  /* 96MHz / 115200 approx */
    USART1_CR1   = (1 << 13) | (1 << 3) | (1 << 2);
}

static void uart_puts(const char *s) {
    while (*s) {
        while (!(USART1_SR & (1 << 7))) {}
        USART1_DR = (uint32_t)(*s++);
    }
}

static void uart_task(void) {
    uart_init();
    while (1) {
        uart_puts("KTOS running on STM32F411CE\r\n");
        volatile uint32_t d = 500000;
        while (d--) __asm volatile ("nop");
    }
}

static void idle_task(void) {
    while (1) {
        __asm volatile ("wfi");
    }
}

int main(void) {
    ktos_Init();
    ktos_CreateTask(&uart_tcb, uart_task, uart_stack, STACK_SIZE);
    ktos_CreateTask(&idle_tcb, idle_task, idle_stack, STACK_SIZE);
    ktos_hal_StartScheduler();
    return 0;
}
