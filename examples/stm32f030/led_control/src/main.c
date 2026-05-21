/*
 * KTOS — STM32F030 Nucleo-F030R8 LED control example (bare-metal)
 *
 * Two cooperating KTOS tasks:
 *
 *   uart_task ('U') — polls USART2 every 10 ms, parses commands:
 *                       ON  → ktos_SendMsg(led_task, MSG_SET_MODE, MODE_ON)
 *                       OFF → ktos_SendMsg(led_task, MSG_SET_MODE, MODE_OFF)
 *                       TOG → ktos_SendMsg(led_task, MSG_SET_MODE, MODE_TOGGLE)
 *
 *   led_task  ('L') — controls PA5 (LD2 on Nucleo, active high).
 *                     In TOGGLE mode, flips every 500 ms.
 *
 * USART2: PA2=TX, PA3=RX (ST-Link virtual COM), 115200 8N1.
 * LED LD2: PA5 (active high).
 * No external hardware required.
 */

#include <stdint.h>
#include <string.h>
#include "../../../../core/ktos.h"

#define RCC_AHBENR   (*(volatile uint32_t *)0x40021014UL)
#define RCC_APB1ENR  (*(volatile uint32_t *)0x4002101CUL)

#define GPIOA_MODER  (*(volatile uint32_t *)0x48000000UL)
#define GPIOA_ODR    (*(volatile uint32_t *)0x48000014UL)
#define GPIOA_BSRR   (*(volatile uint32_t *)0x48000018UL)
#define GPIOA_AFRL   (*(volatile uint32_t *)0x48000020UL)

#define USART2_CR1   (*(volatile uint32_t *)0x40004400UL)
#define USART2_BRR   (*(volatile uint32_t *)0x4000440CUL)
#define USART2_ISR   (*(volatile uint32_t *)0x4000441CUL)
#define USART2_RDR   (*(volatile uint32_t *)0x40004424UL)
#define USART2_TDR   (*(volatile uint32_t *)0x40004428UL)
#define ISR_TXE  (1u << 7)
#define ISR_RXNE (1u << 5)

#define LED_PIN  5u
#define LED_ON()   GPIOA_BSRR = (1u << LED_PIN)
#define LED_OFF()  GPIOA_BSRR = (1u << (LED_PIN + 16u))

static void hw_init(void)
{
    RCC_AHBENR  |= (1u << 17);  /* GPIOA */
    RCC_APB1ENR |= (1u << 17);  /* USART2 */

    /* PA2=TX AF1, PA3=RX AF1, PA5=output */
    GPIOA_MODER = (GPIOA_MODER & ~(0xF0Fu << 4))
                | (2u << 4)    /* PA2 AF */
                | (2u << 6)    /* PA3 AF */
                | (1u << 10);  /* PA5 output */

    GPIOA_AFRL = (GPIOA_AFRL & ~(0xFFu << 8))
               | (1u << 8) | (1u << 12);  /* PA2/PA3 AF1 */

    LED_OFF();
    USART2_BRR = 417u;
    USART2_CR1 = (1u << 0) | (1u << 3) | (1u << 2);
}

static void uart_putc(char c) { while (!(USART2_ISR & ISR_TXE)) {} USART2_TDR = (uint8_t)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static int16_t uart_getc(void)
{
    if (!(USART2_ISR & ISR_RXNE)) { return -1; }
    return (int16_t)(USART2_RDR & 0xFFu);
}

void ktos_timer_irq_handler(void);
void SysTick_Handler(void) { ktos_timer_irq_handler(); }
__attribute__((noreturn)) void ktos_Emergency(const char *msg)
{ uart_puts("\r\n[KTOS FATAL] "); uart_puts(msg); while (1) {} }
void ktos_DebugPrintf(const char *f, ...) { (void)f; }
void ktos_InitSys(void) {}

#define MSG_SET_MODE  ((WORD)(KTOS_MSG_TYPE_SYSTEM_START + 1))
#define MODE_OFF      0U
#define MODE_ON       1U
#define MODE_TOGGLE   2U
#define TOGGLE_MS     500U
#define RX_POLL_MS    10U
#define LINE_BUF      16U

static struct ktos_TASK *g_led_task;

static WORD led_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)lParam;
    static uint8_t mode = MODE_OFF;
    if (MsgType == KTOS_MSG_TYPE_INIT) { LED_OFF(); return KTOS_MSG_SLEEP_INDEFINITLY; }
    if (MsgType == MSG_SET_MODE) {
        mode = (uint8_t)sParam;
        switch (mode) {
        case MODE_ON:  LED_ON();  uart_puts("LED: ON\r\n");        return KTOS_MSG_SLEEP_INDEFINITLY;
        case MODE_OFF: LED_OFF(); uart_puts("LED: OFF\r\n");       return KTOS_MSG_SLEEP_INDEFINITLY;
        default:                  uart_puts("LED: TOGGLE 500ms\r\n"); return TOGGLE_MS;
        }
    }
    if (mode == MODE_TOGGLE) { GPIOA_ODR ^= (1u << LED_PIN); }
    return TOGGLE_MS;
}

static WORD uart_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam; (void)lParam;
    static char buf[LINE_BUF];
    static uint8_t len = 0;
    if (MsgType == KTOS_MSG_TYPE_INIT) {
        uart_puts("=============================\r\n");
        uart_puts("  KTOS LED Control\r\n");
        uart_puts("  LD2 (PA5)  Commands: ON OFF TOG\r\n");
        uart_puts("=============================\r\n");
        len = 0; return RX_POLL_MS;
    }
    int16_t b;
    while ((b = uart_getc()) >= 0) {
        char c = (char)b;
        uart_putc(c);
        if (c == '\r') { uart_putc('\n'); }
        if (c == '\r' || c == '\n') {
            buf[len] = '\0';
            if (strcmp(buf, "ON") == 0 || strcmp(buf, "on") == 0)
                ktos_SendMsg(g_led_task, MSG_SET_MODE, MODE_ON, 0);
            else if (strcmp(buf, "OFF") == 0 || strcmp(buf, "off") == 0)
                ktos_SendMsg(g_led_task, MSG_SET_MODE, MODE_OFF, 0);
            else if (strcmp(buf, "TOG") == 0 || strcmp(buf, "tog") == 0)
                ktos_SendMsg(g_led_task, MSG_SET_MODE, MODE_TOGGLE, 0);
            else if (len > 0) uart_puts("Commands: ON  OFF  TOG\r\n");
            len = 0; continue;
        }
        if (len < (LINE_BUF - 1)) { buf[len++] = c; }
    }
    return RX_POLL_MS;
}

int main(void)
{
    hw_init();
    g_led_task = ktos_InitTask(led_task,  256, 4, 'L');
    ktos_InitTask(uart_task, 256, 4, 'U');
    ktos_RunOS();
    return 0;
}
