/*
 * KTOS — STM32F103 Blue Pill LED control example (bare-metal)
 *
 * Two cooperating KTOS tasks:
 *
 *   uart_task ('U') — polls USART1 every 10 ms, parses commands:
 *                       ON  → ktos_SendMsg(led_task, MSG_SET_MODE, MODE_ON)
 *                       OFF → ktos_SendMsg(led_task, MSG_SET_MODE, MODE_OFF)
 *                       TOG → ktos_SendMsg(led_task, MSG_SET_MODE, MODE_TOGGLE)
 *
 *   led_task  ('L') — controls PC13 (Blue Pill onboard LED, active low).
 *                     In TOGGLE mode, flips every 500 ms.
 *
 * USART1: PA9=TX, PA10=RX, 115200 8N1.
 * LED: PC13 (low = on, high = off).
 */

#include <stdint.h>
#include <string.h>
#include "../../../../core/ktos.h"

/* =========================================================================
 * RCC / GPIO / USART1
 * ========================================================================= */
#define RCC_APB2ENR  (*(volatile uint32_t *)0x40021018UL)

#define GPIOA_CRH    (*(volatile uint32_t *)0x40010804UL)
#define GPIOC_CRH    (*(volatile uint32_t *)0x40011004UL)
#define GPIOC_BSRR   (*(volatile uint32_t *)0x40011010UL)
#define GPIOC_BRR    (*(volatile uint32_t *)0x40011014UL)
#define GPIOC_ODR    (*(volatile uint32_t *)0x4001100CUL)

#define USART1_SR    (*(volatile uint32_t *)0x40013800UL)
#define USART1_DR    (*(volatile uint32_t *)0x40013804UL)
#define USART1_BRR   (*(volatile uint32_t *)0x40013808UL)
#define USART1_CR1   (*(volatile uint32_t *)0x4001380CUL)

#define SR_TXE  (1u << 7)
#define SR_RXNE (1u << 5)

#define LED_PIN  13u
#define LED_ON()   GPIOC_BRR  = (1u << LED_PIN)   /* active low */
#define LED_OFF()  GPIOC_BSRR = (1u << LED_PIN)

static void hw_init(void)
{
    RCC_APB2ENR |= (1u << 2) | (1u << 4) | (1u << 14);  /* GPIOA + GPIOC + USART1 */

    /* PA9=TX AF PP 50 MHz, PA10=RX input floating */
    GPIOA_CRH = (GPIOA_CRH & ~(0xFFu << 4))
              | (0xBu << 4) | (0x4u << 8);

    /* PC13 output push-pull 2 MHz */
    GPIOC_CRH = (GPIOC_CRH & ~(0xFu << 20)) | (0x2u << 20);
    LED_OFF();

    USART1_BRR = 625u;
    USART1_CR1 = (1u << 13) | (1u << 3) | (1u << 2);
}

static void uart_putc(char c) { while (!(USART1_SR & SR_TXE)) {} USART1_DR = (uint8_t)c; }
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static int16_t uart_getc(void)
{
    if (!(USART1_SR & SR_RXNE)) { return -1; }
    return (int16_t)(USART1_DR & 0xFFu);
}

void ktos_timer_irq_handler(void);
void SysTick_Handler(void) { ktos_timer_irq_handler(); }
__attribute__((noreturn)) void ktos_Emergency(const char *msg)
{ uart_puts("\r\n[KTOS FATAL] "); uart_puts(msg); while (1) {} }
void ktos_DebugPrintf(const char *f, ...) { (void)f; }
void ktos_InitSys(void) {}

/* =========================================================================
 * Message types and LED modes
 * ========================================================================= */
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

    if (MsgType == KTOS_MSG_TYPE_INIT) { LED_OFF(); return MSG_WAIT; }

    if (MsgType == MSG_SET_MODE) {
        mode = (uint8_t)sParam;
        switch (mode) {
        case MODE_ON:  LED_ON();  uart_puts("LED: ON\r\n");     return MSG_WAIT;
        case MODE_OFF: LED_OFF(); uart_puts("LED: OFF\r\n");    return MSG_WAIT;
        default:       uart_puts("LED: TOGGLE 500ms\r\n");      return TOGGLE_MS;
        }
    }

    if (mode == MODE_TOGGLE) {
        GPIOC_ODR ^= (1u << LED_PIN);
    }
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
        uart_puts("  Commands: ON  OFF  TOG\r\n");
        uart_puts("=============================\r\n");
        len = 0;
        return RX_POLL_MS;
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
            else if (len > 0)
                uart_puts("Commands: ON  OFF  TOG\r\n");
            len = 0;
            continue;
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
