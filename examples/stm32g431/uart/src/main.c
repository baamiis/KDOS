#include <stdint.h>
#include "ktos.h"

/* USART2 on PA2/PA3, connected to ST-Link VCP on Nucleo-G431RB */
#define RCC_AHB2ENR   (*(volatile uint32_t*)0x4002104C)
#define RCC_APB1ENR1  (*(volatile uint32_t*)0x40021058)
#define GPIOA_MODER   (*(volatile uint32_t*)0x48000000)
#define GPIOA_AFRL    (*(volatile uint32_t*)0x48000020)
#define USART2_CR1    (*(volatile uint32_t*)0x40004400)
#define USART2_BRR    (*(volatile uint32_t*)0x4000440C)
#define USART2_TDR    (*(volatile uint32_t*)0x40004428)
#define USART2_ISR    (*(volatile uint32_t*)0x4000441C)

#define SYS_CLOCK_HZ  170000000UL
#define BAUD          115200UL

static void uart_init(void) {
    RCC_AHB2ENR  |= (1 << 0);   /* GPIOAEN */
    RCC_APB1ENR1 |= (1 << 17);  /* USART2EN */
    /* PA2 AF7, PA3 AF7 */
    GPIOA_MODER &= ~((3<<4)|(3<<6));
    GPIOA_MODER |=  ((2<<4)|(2<<6));
    GPIOA_AFRL  &= ~((0xF<<8)|(0xF<<12));
    GPIOA_AFRL  |=  ((7<<8)|(7<<12));
    USART2_BRR   = SYS_CLOCK_HZ / BAUD;
    USART2_CR1   = (1<<3)|(1<<0); /* TE | UE */
}

static void uart_puts(const char *s) {
    while (*s) {
        while (!(USART2_ISR & (1<<7)));
        USART2_TDR = (uint8_t)*s++;
    }
}

#define STACK_SIZE 128
static uint32_t uart_stack[STACK_SIZE];

static WORD uart_task(WORD MsgType, WORD Param1, LONG Param2) {
    (void)Param1; (void)Param2;
    switch (MsgType) {
        case KTOS_MSG_TYPE_INIT:
            uart_init();
            uart_puts("KTOS STM32G431 UART ready\r\n");
            break;
        case KTOS_MSG_TYPE_TIMER:
            uart_puts("hello from KTOS\r\n");
            break;
    }
    return 1000;
}

int main(void) {
    ktos_Init();
    ktos_CreateTask(uart_task, uart_stack + STACK_SIZE);
    ktos_Start();
    for (;;);
}
