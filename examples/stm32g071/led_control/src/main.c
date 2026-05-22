#include <stdint.h>
#include "../../../../core/ktos.h"

/* GPIOA on STM32G071: PA5 = onboard LED (Nucleo-G071RB) */
#define RCC_IOPENR   (*((volatile uint32_t*)0x40021034))
#define GPIOA_MODER  (*((volatile uint32_t*)0x50000000))
#define GPIOA_ODR    (*((volatile uint32_t*)0x50000014))
#define LED_PIN      5u

static uint8_t task_stack[256];

static WORD led_task(WORD MsgType, WORD Param1, LONG Param2) {
    (void)Param1; (void)Param2;
    switch (MsgType) {
        case KTOS_MSG_TYPE_INIT:
            RCC_IOPENR  |= (1u << 0);                     /* GPIOA clock */
            GPIOA_MODER &= ~(3u << (LED_PIN * 2));
            GPIOA_MODER |=  (1u << (LED_PIN * 2));        /* output */
            break;
        case KTOS_MSG_TYPE_TIMER:
            GPIOA_ODR ^= (1u << LED_PIN);                 /* toggle */
            break;
    }
    return 500;
}

int main(void) {
    KTOS_Init();
    KTOS_TaskCreate(led_task, task_stack, sizeof(task_stack));
    KTOS_Start();
    for (;;);
}
