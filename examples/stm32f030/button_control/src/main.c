/*
 * KTOS — STM32F030 Nucleo-F030R8 button control example (bare-metal)
 *
 * Two cooperating KTOS tasks:
 *
 *   btn_task ('B') — polls PC13 (B1 USER button, active low) every 5 ms
 *                    with a 30 ms debouncer. On confirmed press, sends
 *                    MSG_BUTTON_EVENT to led_task.
 *
 *   led_task ('L') — toggles PA5 (LD2, active high) on each event
 *                    and reports via USART2.
 *
 * Hardware (all on-board on Nucleo):
 *   PC13 — B1 USER button (active low, board pull-up)
 *   PA5  — LD2 onboard LED (active high)
 *   PA2  — USART2 TX → ST-Link virtual COM port
 */

#include <stdint.h>
#include "../../../../core/ktos.h"

#define RCC_AHBENR   (*(volatile uint32_t *)0x40021014UL)
#define RCC_APB1ENR  (*(volatile uint32_t *)0x4002101CUL)

#define GPIOA_MODER  (*(volatile uint32_t *)0x48000000UL)
#define GPIOA_ODR    (*(volatile uint32_t *)0x48000014UL)
#define GPIOA_BSRR   (*(volatile uint32_t *)0x48000018UL)
#define GPIOA_AFRL   (*(volatile uint32_t *)0x48000020UL)

#define GPIOC_MODER  (*(volatile uint32_t *)0x48000800UL)
#define GPIOC_PUPDR  (*(volatile uint32_t *)0x4800080CUL)
#define GPIOC_IDR    (*(volatile uint32_t *)0x48000810UL)

#define USART2_CR1   (*(volatile uint32_t *)0x40004400UL)
#define USART2_BRR   (*(volatile uint32_t *)0x4000440CUL)
#define USART2_ISR   (*(volatile uint32_t *)0x4000441CUL)
#define USART2_TDR   (*(volatile uint32_t *)0x40004428UL)
#define ISR_TXE  (1u << 7)

#define LED_PIN   5u
#define BTN_PIN  13u
#define LED_ON()   GPIOA_BSRR = (1u << LED_PIN)
#define LED_OFF()  GPIOA_BSRR = (1u << (LED_PIN + 16u))
#define BTN_PRESSED()  (!(GPIOC_IDR & (1u << BTN_PIN)))

static void hw_init(void)
{
    RCC_AHBENR  |= (1u << 17) | (1u << 19);  /* GPIOA + GPIOC */
    RCC_APB1ENR |= (1u << 17);                /* USART2 */

    /* PA2=TX AF1 */
    GPIOA_MODER = (GPIOA_MODER & ~(3u << 4)) | (2u << 4);
    GPIOA_AFRL  = (GPIOA_AFRL  & ~(0xFu << 8)) | (1u << 8);

    /* PA5=output */
    GPIOA_MODER = (GPIOA_MODER & ~(3u << 10)) | (1u << 10);
    LED_OFF();

    /* PC13=input, pull-up */
    GPIOC_MODER &= ~(3u << 26);         /* input */
    GPIOC_PUPDR  = (GPIOC_PUPDR & ~(3u << 26)) | (1u << 26);  /* pull-up */

    USART2_BRR = 417u;
    USART2_CR1 = (1u << 0) | (1u << 3);  /* UE | TE */
}

static void uart_putc(char c) { while (!(USART2_ISR & ISR_TXE)) {} USART2_TDR = (uint8_t)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

void ktos_timer_irq_handler(void);
void SysTick_Handler(void) { ktos_timer_irq_handler(); }
__attribute__((noreturn)) void ktos_Emergency(const char *msg)
{ uart_puts("\r\n[KTOS FATAL] "); uart_puts(msg); while (1) {} }
void ktos_DebugPrintf(const char *f, ...) { (void)f; }
void ktos_InitSys(void) {}

#define MSG_BUTTON_EVENT  ((WORD)(KTOS_MSG_TYPE_SYSTEM_START + 1))
#define DEBOUNCE_MS  30U
#define POLL_MS       5U

static struct ktos_TASK *g_led_task;
static uint32_t g_press_count;

static WORD led_task(WORD MsgType, WORD Param1, LONG Param2)
{
    (void)Param1; (void)Param2;
    if (MsgType == KTOS_MSG_TYPE_INIT) { LED_OFF(); return KTOS_MSG_SLEEP_INDEFINITLY; }
    if (MsgType != MSG_BUTTON_EVENT)   { return KTOS_MSG_SLEEP_INDEFINITLY; }
    ++g_press_count;
    GPIOA_ODR ^= (1u << LED_PIN);
    uart_puts("Button #");
    uint32_t n = g_press_count;
    char buf[11]; uint8_t i = 0;
    do { buf[i++] = (char)('0' + n % 10u); n /= 10u; } while (n);
    while (i--) uart_putc(buf[i]);
    uart_puts(" — LD2 toggled\r\n");
    return KTOS_MSG_SLEEP_INDEFINITLY;
}

static WORD btn_task(WORD MsgType, WORD Param1, LONG Param2)
{
    (void)Param1; (void)Param2;
    static uint8_t last_state  = 0;
    static uint8_t debounce_ms = 0;
    if (MsgType == KTOS_MSG_TYPE_INIT) {
        uart_puts("=============================\r\n");
        uart_puts("  KTOS Button Control\r\n");
        uart_puts("  B1(PC13) → LD2(PA5)\r\n");
        uart_puts("  30 ms debounce\r\n");
        uart_puts("=============================\r\n");
        return POLL_MS;
    }
    uint8_t pressed = BTN_PRESSED() ? 1u : 0u;
    if (pressed != last_state) { debounce_ms = 0; last_state = pressed; }
    else if (debounce_ms < DEBOUNCE_MS) {
        debounce_ms = (uint8_t)(debounce_ms + POLL_MS);
        if (debounce_ms >= DEBOUNCE_MS && pressed)
            ktos_SendMsg(g_led_task, MSG_BUTTON_EVENT, 0, 0);
    }
    return POLL_MS;
}

int main(void)
{
    hw_init();
    g_led_task = ktos_InitTask(led_task, 256, 4, 'L');
    ktos_InitTask(btn_task, 256, 4, 'B');
    ktos_RunOS();
    return 0;
}
