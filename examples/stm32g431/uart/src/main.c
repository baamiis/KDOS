/*
 * KTOS UART Example — STM32G431
 * Sends "KTOS\r\n" over LPUART1 (PA2/PA3) every 1 second.
 */
#include "ktos.h"

/* RCC */
#define RCC_AHB2ENR   (*((volatile unsigned int *)0x4002104C))
#define RCC_APB1ENR2  (*((volatile unsigned int *)0x40021060))
/* GPIOA */
#define GPIOA_MODER   (*((volatile unsigned int *)0x48000000))
#define GPIOA_AFRL    (*((volatile unsigned int *)0x48000020))
/* LPUART1 */
#define LPUART1_CR1   (*((volatile unsigned int *)0x40008000))
#define LPUART1_BRR   (*((volatile unsigned int *)0x40008008))
#define LPUART1_ISR   (*((volatile unsigned int *)0x4000801C))
#define LPUART1_TDR   (*((volatile unsigned int *)0x40008028))

static void uart_init(void) {
    RCC_AHB2ENR  |= (1u << 0);   /* GPIOA */
    RCC_APB1ENR2 |= (1u << 0);   /* LPUART1 */
    /* PA2 AF12, PA3 AF12 */
    GPIOA_MODER &= ~(0xFu << 4);
    GPIOA_MODER |=  (0xAu << 4); /* alternate function */
    GPIOA_AFRL  &= ~(0xFFu << 8);
    GPIOA_AFRL  |=  (0xCCu << 8); /* AF12 for PA2 and PA3 */
    /* 115200 @ 170 MHz: BRR = 170000000*256/115200 = 377778 */
    LPUART1_BRR  = 377778u;
    LPUART1_CR1  = (1u << 3) | (1u << 2) | (1u << 0); /* TE | RE | UE */
}

static void uart_send(const char *s) {
    while (*s) {
        while (!(LPUART1_ISR & (1u << 7))) {}
        LPUART1_TDR = (unsigned int)*s++;
    }
}

WORD task_uart(WORD MsgType, WORD Param1, LONG Param2) {
    (void)Param1; (void)Param2;
    switch (MsgType) {
        case KTOS_MSG_TYPE_INIT:
            uart_init();
            break;
        case KTOS_MSG_TYPE_TIMER:
            uart_send("KTOS\r\n");
            break;
        default:
            break;
    }
    return 1000;
}

#define UART_STACK_SIZE 256u
static unsigned char uart_stack[UART_STACK_SIZE];

int main(void) {
    ktos_Init();
    ktos_TaskCreate(task_uart, uart_stack, UART_STACK_SIZE);
    ktos_RunOS();
    for (;;) {}
}
