#include <stdint.h>
#include "ktos.h"

/* PA5 = LD2 on Nucleo-G431RB */
#define RCC_AHB2ENR  (*(volatile uint32_t*)0x4002104C)
#define GPIOA_MODER  (*(volatile uint32_t*)0x48000000)
#define GPIOA_ODR    (*(volatile uint32_t*)0x48000014)

#define STACK_SIZE 128
static uint32_t led_stack[STACK_SIZE];

static WORD led_task(WORD MsgType, WORD Param1, LONG Param2) {
    (void)Param1; (void)Param2;
    switch (MsgType) {
        case KTOS_MSG_TYPE_INIT:
            RCC_AHB2ENR |= (1 << 0);          /* GPIOAEN */
            GPIOA_MODER &= ~(3 << 10);
            GPIOA_MODER |=  (1 << 10);        /* PA5 output */
            break;
        case KTOS_MSG_TYPE_TIMER:
            GPIOA_ODR ^= (1 << 5);            /* toggle LED */
            break;
    }
    return 500;
}

int main(void) {
    ktos_Init();
    ktos_CreateTask(led_task, led_stack + STACK_SIZE);
    ktos_Start();
    for (;;);
}
