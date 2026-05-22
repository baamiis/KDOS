#include <stdint.h>
#include "ktos.h"

#define RCC_APB2ENR  (*((volatile uint32_t *)0x40021018))
#define GPIOC_CRH    (*((volatile uint32_t *)0x40011004))
#define GPIOC_ODR    (*((volatile uint32_t *)0x4001100C))
#define LED_PIN      13

#define STACK_SIZE 128
static uint32_t task1_stack[STACK_SIZE];
static uint32_t task2_stack[STACK_SIZE];

static void delay(volatile uint32_t n) {
    while (n--) __asm volatile ("NOP");
}

static void led_on_task(void) {
    for (;;) {
        GPIOC_ODR &= ~(1u << LED_PIN);
        delay(500000);
        ktos_yield();
    }
}

static void led_off_task(void) {
    for (;;) {
        GPIOC_ODR |= (1u << LED_PIN);
        delay(500000);
        ktos_yield();
    }
}

int main(void) {
    RCC_APB2ENR |= (1u << 4);
    GPIOC_CRH   &= ~(0xFu << 20);
    GPIOC_CRH   |=  (0x2u << 20);

    ktos_init();
    ktos_task_create(led_on_task,  task1_stack, STACK_SIZE);
    ktos_task_create(led_off_task, task2_stack, STACK_SIZE);
    ktos_hal_InitSystemTimer();
    ktos_hal_StartScheduler();
    for (;;);
}
