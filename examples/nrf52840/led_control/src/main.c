#include <stdint.h>
#include "ktos.h"

/* NRF52840 GPIO P0 registers */
#define P0_BASE       0x50000000UL
#define P0_DIRSET     (*((volatile uint32_t*)(P0_BASE + 0x518)))
#define P0_OUTSET     (*((volatile uint32_t*)(P0_BASE + 0x508)))
#define P0_OUTCLR     (*((volatile uint32_t*)(P0_BASE + 0x50C)))

/* LED1 = P0.13 on nRF52840 DK */
#define LED1_PIN      13
#define LED1_MASK     (1UL << LED1_PIN)

#define STACK_SIZE    256

static uint32_t task1_stack[STACK_SIZE];
static uint32_t task2_stack[STACK_SIZE];
static ktos_tcb_t task1_tcb;
static ktos_tcb_t task2_tcb;

static void delay(volatile uint32_t count) {
    while (count--) __asm volatile ("nop");
}

static void led_on_task(void *arg) {
    (void)arg;
    P0_DIRSET = LED1_MASK;
    while (1) {
        P0_OUTCLR = LED1_MASK; /* Active low */
        delay(200000);
        ktos_yield();
    }
}

static void led_off_task(void *arg) {
    (void)arg;
    while (1) {
        P0_OUTSET = LED1_MASK; /* Active low: off */
        delay(200000);
        ktos_yield();
    }
}

int main(void) {
    ktos_init();
    ktos_task_create(&task1_tcb, led_on_task, NULL, task1_stack, STACK_SIZE);
    ktos_task_create(&task2_tcb, led_off_task, NULL, task2_stack, STACK_SIZE);
    ktos_start();
    while (1) {}
}
