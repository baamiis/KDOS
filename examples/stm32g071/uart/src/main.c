#include <stdint.h>
#include "../../../../core/ktos.h"

/* USART2 on STM32G071 (PA2=TX, PA3=RX), 64 MHz, 115200 baud */
#define RCC_IOPENR    (*((volatile uint32_t*)0x40021034))
#define RCC_APBENR1   (*((volatile uint32_t*)0x4002103C))
#define GPIOA_MODER   (*((volatile uint32_t*)0x50000000))
#define GPIOA_AFRL    (*((volatile uint32_t*)0x50000020))
#define USART2_CR1    (*((volatile uint32_t*)0x40004400))
#define USART2_BRR    (*((volatile uint32_t*)0x4000440C))
#define USART2_TDR    (*((volatile uint32_t*)0x40004428))
#define USART2_ISR    (*((volatile uint32_t*)0x4000441C))

static uint8_t task_stack[256];
static const char msg[] = "KTOS STM32G071 UART\r\n";

static void uart_init(void) {
    RCC_IOPENR  |= (1u << 0);    /* GPIOA */
    RCC_APBENR1 |= (1u << 17);   /* USART2 */
    /* PA2 AF1, PA3 AF1 */
    GPIOA_MODER &= ~((3u<<4)|(3u<<6));
    GPIOA_MODER |=  ((2u<<4)|(2u<<6));
    GPIOA_AFRL  &= ~((0xFu<<8)|(0xFu<<12));
    GPIOA_AFRL  |=  ((1u<<8)|(1u<<12));
    USART2_BRR   = 64000000u / 115200u;
    USART2_CR1   = (1u<<3)|(1u<<0); /* TE | UE */
}

static void uart_puts(const char *s) {
    while (*s) {
        while (!(USART2_ISR & (1u<<7)));
        USART2_TDR = (uint8_t)*s++;
    }
}

static WORD uart_task(WORD MsgType, WORD Param1, LONG Param2) {
    (void)Param1; (void)Param2;
    switch (MsgType) {
        case KTOS_MSG_TYPE_INIT:  uart_init(); break;
        case KTOS_MSG_TYPE_TIMER: uart_puts(msg); break;
    }
    return 1000;
}

int main(void) {
    KTOS_Init();
    KTOS_TaskCreate(uart_task, task_stack, sizeof(task_stack));
    KTOS_Start();
    for (;;);
}
