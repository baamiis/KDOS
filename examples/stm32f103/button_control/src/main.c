/*
 * KTOS — STM32F103 Blue Pill button control example (bare-metal)
 *
 * Two cooperating KTOS tasks:
 *
 *   btn_task ('B') — polls PB0 every 5 ms with a 30 ms debouncer.
 *                    On a confirmed press, sends MSG_BUTTON_EVENT to led_task.
 *
 *   led_task ('L') — toggles PC13 (onboard LED, active low) on each
 *                    MSG_BUTTON_EVENT and reports via USART1.
 *
 * Hardware:
 *   PB0 — tactile button to GND (internal pull-up enabled).
 *   PC13 — Blue Pill onboard LED (active low).
 *   PA9  — USART1 TX → USB-serial adapter (3.3 V).
 *
 * Tick: SysTick 1 ms at 72 MHz.
 */

#include <stdint.h>
#include "../../../../core/ktos.h"

/* =========================================================================
 * RCC / GPIO / USART1
 * ========================================================================= */
#define RCC_APB2ENR  (*(volatile uint32_t *)0x40021018UL)

#define GPIOA_CRH    (*(volatile uint32_t *)0x40010804UL)
#define GPIOB_CRL    (*(volatile uint32_t *)0x40010C00UL)
#define GPIOB_ODR    (*(volatile uint32_t *)0x40010C0CUL)
#define GPIOB_IDR    (*(volatile uint32_t *)0x40010C08UL)
#define GPIOC_CRH    (*(volatile uint32_t *)0x40011004UL)
#define GPIOC_BSRR   (*(volatile uint32_t *)0x40011010UL)
#define GPIOC_BRR    (*(volatile uint32_t *)0x40011014UL)
#define GPIOC_ODR    (*(volatile uint32_t *)0x4001100CUL)

#define USART1_SR    (*(volatile uint32_t *)0x40013800UL)
#define USART1_DR    (*(volatile uint32_t *)0x40013804UL)
#define USART1_BRR   (*(volatile uint32_t *)0x40013808UL)
#define USART1_CR1   (*(volatile uint32_t *)0x4001380CUL)
#define SR_TXE  (1u << 7)

#define LED_PIN  13u
#define BTN_PIN   0u
#define LED_ON()   GPIOC_BRR  = (1u << LED_PIN)
#define LED_OFF()  GPIOC_BSRR = (1u << LED_PIN)
#define BTN_PRESSED()  (!(GPIOB_IDR & (1u << BTN_PIN)))

static void hw_init(void)
{
    /* Enable GPIOA + GPIOB + GPIOC + USART1 */
    RCC_APB2ENR |= (1u << 2) | (1u << 3) | (1u << 4) | (1u << 14);

    /* PA9=TX AF PP 50 MHz */
    GPIOA_CRH = (GPIOA_CRH & ~(0xFu << 4)) | (0xBu << 4);

    /* PB0=input pull-up (CNF=10, MODE=00 → 0x8); enable pull-up via ODR */
    GPIOB_CRL = (GPIOB_CRL & ~0xFu) | 0x8u;
    GPIOB_ODR |= (1u << BTN_PIN);

    /* PC13=output push-pull 2 MHz */
    GPIOC_CRH = (GPIOC_CRH & ~(0xFu << 20)) | (0x2u << 20);
    LED_OFF();

    USART1_BRR = 625u;
    USART1_CR1 = (1u << 13) | (1u << 3);  /* UE | TE */
}

static void uart_putc(char c) { while (!(USART1_SR & SR_TXE)) {} USART1_DR = (uint8_t)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }

void ktos_timer_irq_handler(void);
void SysTick_Handler(void) { ktos_timer_irq_handler(); }
__attribute__((noreturn)) void ktos_Emergency(const char *msg)
{ uart_puts("\r\n[KTOS FATAL] "); uart_puts(msg); while (1) {} }
void ktos_DebugPrintf(const char *f, ...) { (void)f; }
void ktos_InitSys(void) {}

/* =========================================================================
 * Tasks
 * ========================================================================= */
#define MSG_BUTTON_EVENT  ((WORD)(KTOS_MSG_TYPE_SYSTEM_START + 1))
#define DEBOUNCE_MS       30U
#define POLL_MS            5U

static struct ktos_TASK *g_led_task;
static uint32_t g_press_count;

static WORD led_task(WORD MsgType, WORD Param1, LONG Param2)
{
    (void)Param1; (void)Param2;
    if (MsgType == KTOS_MSG_TYPE_INIT) { LED_OFF(); return KTOS_MSG_SLEEP_INDEFINITLY; }
    if (MsgType != MSG_BUTTON_EVENT)   { return KTOS_MSG_SLEEP_INDEFINITLY; }
    ++g_press_count;
    GPIOC_ODR ^= (1u << LED_PIN);
    uart_puts("Button press #");
    uint32_t n = g_press_count;
    char buf[11]; uint8_t i = 0;
    do { buf[i++] = (char)('0' + n % 10u); n /= 10u; } while (n);
    while (i--) uart_putc(buf[i]);
    uart_puts(" — LED toggled\r\n");
    return KTOS_MSG_SLEEP_INDEFINITLY;
}

static WORD btn_task(WORD MsgType, WORD Param1, LONG Param2)
{
    (void)Param1; (void)Param2;
    static uint8_t last_state  = 0;  /* 0=released */
    static uint8_t debounce_ms = 0;

    if (MsgType == KTOS_MSG_TYPE_INIT) {
        uart_puts("=============================\r\n");
        uart_puts("  KTOS Button Control\r\n");
        uart_puts("  PB0 button → PC13 LED\r\n");
        uart_puts("  30 ms debounce\r\n");
        uart_puts("=============================\r\n");
        return POLL_MS;
    }

    uint8_t pressed = BTN_PRESSED() ? 1u : 0u;
    if (pressed != last_state) {
        debounce_ms = 0;
        last_state = pressed;
    } else if (debounce_ms < DEBOUNCE_MS) {
        debounce_ms = (uint8_t)(debounce_ms + POLL_MS);
        if (debounce_ms >= DEBOUNCE_MS && pressed) {
            ktos_SendMsg(g_led_task, MSG_BUTTON_EVENT, 0, 0);
        }
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
