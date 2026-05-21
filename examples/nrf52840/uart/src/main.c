#include <stdint.h>
#include <string.h>
#include "ktos.h"

/* NRF52840 UART0 (UARTE0) */
#define UARTE0_BASE       0x40002000UL
#define UARTE0_STARTTX    (*((volatile uint32_t*)(UARTE0_BASE + 0x008)))
#define UARTE0_EVENTS_ENDTX (*((volatile uint32_t*)(UARTE0_BASE + 0x120)))
#define UARTE0_ENABLE     (*((volatile uint32_t*)(UARTE0_BASE + 0x500)))
#define UARTE0_PSEL_TXD   (*((volatile uint32_t*)(UARTE0_BASE + 0x50C)))
#define UARTE0_BAUDRATE   (*((volatile uint32_t*)(UARTE0_BASE + 0x524)))
#define UARTE0_TXD_PTR    (*((volatile uint32_t*)(UARTE0_BASE + 0x544)))
#define UARTE0_TXD_MAXCNT (*((volatile uint32_t*)(UARTE0_BASE + 0x548)))

/* TX pin = P0.6 on nRF52840 DK */
#define UART_TX_PIN  6
#define BAUD_115200  0x01D7E000UL

#define STACK_SIZE   256

static uint32_t tx_stack[STACK_SIZE];
static uint32_t idle_stack[STACK_SIZE];
static ktos_tcb_t tx_tcb;
static ktos_tcb_t idle_tcb;

static void uart_init(void) {
    UARTE0_PSEL_TXD   = UART_TX_PIN;
    UARTE0_BAUDRATE   = BAUD_115200;
    UARTE0_ENABLE     = 8; /* Enable UARTE */
}

static void uart_send(const char *str) {
    uint32_t len = strlen(str);
    UARTE0_EVENTS_ENDTX = 0;
    UARTE0_TXD_PTR      = (uint32_t)str;
    UARTE0_TXD_MAXCNT   = len;
    UARTE0_STARTTX      = 1;
    while (!UARTE0_EVENTS_ENDTX) {}
}

static void uart_task(void *arg) {
    (void)arg;
    uart_init();
    while (1) {
        uart_send("KTOS NRF52840 UART\r\n");
        ktos_delay(500);
    }
}

static void idle_task(void *arg) {
    (void)arg;
    while (1) {
        __asm volatile ("wfe");
        ktos_yield();
    }
}

int main(void) {
    ktos_init();
    ktos_task_create(&tx_tcb,   uart_task, NULL, tx_stack,   STACK_SIZE);
    ktos_task_create(&idle_tcb, idle_task, NULL, idle_stack, STACK_SIZE);
    ktos_start();
    while (1) {}
}
