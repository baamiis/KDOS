#include <stdint.h>
#include "ktos.h"

#define RCC_AHB1ENR  (*((volatile uint32_t *)0x40023830))
#define GPIOC_MODER  (*((volatile uint32_t *)0x40020800))
#define GPIOC_ODR    (*((volatile uint32_t *)0x40020814))
#define LED_PIN      13

#define STACK_SIZE   128

static uint32_t led_stack[STACK_SIZE];
static uint32_t idle_stack[STACK_SIZE];

static ktos_tcb_t led_tcb;
static ktos_tcb_t idle_tcb;

static void delay(volatile uint32_t n) {
    while (n--) __asm volatile ("nop");
}

static void led_task(void) {
    RCC_AHB1ENR  |= (1 << 2);
    GPIOC_MODER  &= ~(3U << (LED_PIN * 2));
    GPIOC_MODER  |=  (1U << (LED_PIN * 2));
    while (1) {
        GPIOC_ODR ^= (1 << LED_PIN);
        delay(400000);
    }
}

static void idle_task(void) {
    while (1) {
        __asm volatile ("wfi");
    }
}

int main(void) {
    ktos_Init();
    ktos_CreateTask(&led_tcb,  led_task,  led_stack,  STACK_SIZE);
    ktos_CreateTask(&idle_tcb, idle_task, idle_stack, STACK_SIZE);
    ktos_hal_StartScheduler();
    return 0;
}
