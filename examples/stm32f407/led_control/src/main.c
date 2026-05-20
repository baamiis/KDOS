#include <stdint.h>
#include "ktos.h"

/* STM32F407 GPIOD (LEDs on PD12-PD15) */
#define RCC_AHB1ENR  (*(volatile uint32_t*)0x40023830)
#define GPIOD_MODER  (*(volatile uint32_t*)0x40020C00)
#define GPIOD_ODR    (*(volatile uint32_t*)0x40020C14)

#define LED_GREEN  (1U << 12)
#define LED_ORANGE (1U << 13)
#define LED_RED    (1U << 14)
#define LED_BLUE   (1U << 15)

static void delay(volatile uint32_t n) {
    while (n--) __asm volatile ("nop");
}

static void gpio_init(void) {
    RCC_AHB1ENR |= (1U << 3);  /* GPIOD clock */
    /* Set PD12-PD15 as output */
    GPIOD_MODER &= ~(0xFFU << 24);
    GPIOD_MODER |=  (0x55U << 24);
}

KTOS_STACK_DEF(task1_stack, 256);
KTOS_STACK_DEF(task2_stack, 256);

static void task_green_orange(void) {
    while (1) {
        GPIOD_ODR ^= LED_GREEN;
        delay(500000);
        GPIOD_ODR ^= LED_ORANGE;
        delay(500000);
        ktos_yield();
    }
}

static void task_red_blue(void) {
    while (1) {
        GPIOD_ODR ^= LED_RED;
        delay(500000);
        GPIOD_ODR ^= LED_BLUE;
        delay(500000);
        ktos_yield();
    }
}

int main(void) {
    gpio_init();
    ktos_init();
    ktos_task_create(task_green_orange, task1_stack, sizeof(task1_stack));
    ktos_task_create(task_red_blue,     task2_stack, sizeof(task2_stack));
    ktos_start();
    while (1);
}
