#include <stdint.h>
#include "ktos.h"

/* PA5 = LD2 on Nucleo-G431RB */
#define RCC_AHB2ENR  (*(volatile uint32_t*)0x4002104C)
#define GPIOA_MODER  (*(volatile uint32_t*)0x48000000)
#define GPIOA_ODR    (*(volatile uint32_t*)0x48000014)

#define STACK_SIZE 128
static uint32_t led_stack[STACK_SIZE];

static WORD led_task(WORD msg, WORD p1, LONG p2) {
    (void)p1; (void)p2;
    if (msg == KTOS_MSG_TYPE_INIT) {
        RCC_AHB2ENR |= (1u << 0);
        GPIOA_MODER  = (GPIOA_MODER & ~(3u << 10)) | (1u << 10);
    } else {
        GPIOA_ODR ^= (1u << 5);
    }
    return 500;
}

static KTOS_TASK tasks[1];

int main(void) {
    KTOS_Init(tasks, 1);
    KTOS_CreateTask(led_task, led_stack, sizeof(led_stack));
    KTOS_Start();
    for (;;);
}
