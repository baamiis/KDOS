/*
 * KTOS LED Control Example — STM32G431
 * Blinks the onboard LED (PA5) every 500 ms using a KTOS task.
 */
#include "ktos.h"

/* GPIOA registers */
#define RCC_AHB2ENR   (*((volatile unsigned int *)0x4002104C))
#define GPIOA_MODER   (*((volatile unsigned int *)0x48000000))
#define GPIOA_ODR     (*((volatile unsigned int *)0x48000014))
#define GPIOA_BSRR    (*((volatile unsigned int *)0x48000018))

#define LED_PIN 5u

static void led_init(void) {
    RCC_AHB2ENR |= (1u << 0);                      /* GPIOA clock */
    GPIOA_MODER &= ~(3u << (LED_PIN * 2u));
    GPIOA_MODER |=  (1u << (LED_PIN * 2u));        /* output */
}

WORD task_led(WORD MsgType, WORD Param1, LONG Param2) {
    (void)Param1; (void)Param2;
    switch (MsgType) {
        case KTOS_MSG_TYPE_INIT:
            led_init();
            break;
        case KTOS_MSG_TYPE_TIMER:
            GPIOA_ODR ^= (1u << LED_PIN);           /* toggle */
            break;
        default:
            break;
    }
    return 500;
}

#define LED_STACK_SIZE 256u
static unsigned char led_stack[LED_STACK_SIZE];

int main(void) {
    ktos_Init();
    ktos_TaskCreate(task_led, led_stack, LED_STACK_SIZE);
    ktos_RunOS();
    for (;;) {}
}
