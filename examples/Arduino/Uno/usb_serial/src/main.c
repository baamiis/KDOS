/*
 * KTOS - Arduino Uno USB-serial CLI example (bare-metal)
 *
 * Two cooperating KTOS tasks form an interactive shell over the Uno's
 * USB CDC link.  The on-board ATmega16U2 implements the USB CDC class
 * on the PC side and bridges it to USART0 on the application MCU, so
 * the 328P just talks to USART0 like any other UART.
 *
 *   rx_task  ('R') -- wakes every 10 ms, drains USART0, echoes every
 *                     byte for instant feedback, and assembles a line
 *                     in a fixed 64-byte buffer.  On '\r' or '\n' it
 *                     posts ktos_SendMsg(MSG_LINE_READY) to cmd_task.
 *
 *   cmd_task ('C') -- sleeps with KTOS_MSG_SLEEP_INDEFINITLY.  On MSG_LINE_READY it
 *                     parses the first whitespace-delimited token
 *                     and dispatches:
 *
 *                       HELP                   list commands
 *                       INFO                   board + KTOS info
 *                       LED ON | OFF | TOGGLE  drive D13 (PB5)
 *                       ADC                    one A0 sample
 *                       BTN                    one D2 read (pull-up)
 *                       ECHO <text>            print the rest of the line
 *
 * No Arduino framework.  Direct register access for USART0, ADC,
 * PORTB/D, and PIND.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "../../../../../core/ktos.h"

/* =========================================================================
 * USART0 - 115200 8N1 (U2X0=1, UBRR0=16)
 * ========================================================================= */

static void uart_init(void)
{
    UBRR0H = 0;
    UBRR0L = 16;
    UCSR0A = (1 << U2X0);
    UCSR0B = (1 << TXEN0) | (1 << RXEN0);
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

static void uart_putc(char c)
{
    while (!(UCSR0A & (1 << UDRE0))) { }
    UDR0 = (uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s) { uart_putc(*s++); }
}

static int16_t uart_get_byte(void)
{
    if (!(UCSR0A & (1 << RXC0))) { return -1; }
    return (int16_t)UDR0;
}

static void uart_putu16(uint16_t n)
{
    char buf[6];
    uint8_t i = 0;
    if (n == 0) { uart_putc('0'); return; }
    while (n) { buf[i++] = (char)('0' + (n % 10)); n /= 10; }
    while (i--) { uart_putc(buf[i]); }
}

/* =========================================================================
 * LED on D13 = PB5
 * ========================================================================= */

static inline void led_init(void)   { DDRB |= (1 << PORTB5); }
static inline void led_on(void)     { PORTB |= (1 << PORTB5); }
static inline void led_off(void)    { PORTB &= (uint8_t)~(1 << PORTB5); }
static inline void led_toggle(void) { PORTB ^= (1 << PORTB5); }
static inline bool led_is_on(void)  { return (PORTB & (1 << PORTB5)) != 0; }

/* =========================================================================
 * Button on D2 = PD2 (active-low with internal pull-up)
 * ========================================================================= */

static inline void btn_init(void)
{
    DDRD  &= (uint8_t)~(1 << PORTD2);
    PORTD |= (1 << PORTD2);
}

static inline bool btn_pressed(void)
{
    return (PIND & (1 << PORTD2)) == 0;
}

/* =========================================================================
 * ADC - channel 0 (A0 / PC0), AVcc reference, prescaler 128
 * ========================================================================= */

static void adc_init(void)
{
    ADMUX  = (1 << REFS0);
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
}

static uint16_t adc_read(uint8_t channel)
{
    ADMUX = (ADMUX & (uint8_t)~0x0F) | (channel & 0x0F);
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC)) { }
    return ADC;
}

/* =========================================================================
 * KTOS platform callbacks
 * ========================================================================= */

__attribute__((noreturn))
void ktos_Emergency(const char *msg)
{
    uart_puts("\r\n[KTOS FATAL] ");
    uart_puts(msg);
    uart_puts("\r\n");
    while (1) { }
}

void ktos_DebugPrintf(const char *fmt, ...) { (void)fmt; }
void ktos_InitSys(void) { }

/* =========================================================================
 * KTOS 1 ms tick - Timer1 CTC
 * ========================================================================= */

extern void ktos_timer_irq_handler(void);

ISR(TIMER1_COMPA_vect)
{
    ktos_timer_irq_handler();
}

/* =========================================================================
 * Line buffer + tokenizer
 * ========================================================================= */

#define MSG_LINE_READY  ((WORD)(KTOS_MSG_TYPE_SYSTEM_START + 1))
#define LINE_BUF_SIZE   64U
#define RX_POLL_MS      10U

static char    g_line_buf[LINE_BUF_SIZE];
static uint8_t g_line_len = 0;

static struct ktos_TASK *g_cmd_task = NULL;

/* Walks *p over whitespace, returns pointer to next token (null-
 * terminated), advances *p past it.  Returns NULL when no token. */
static char *next_token(char **p)
{
    char *s = *p;
    while (*s == ' ' || *s == '\t') { ++s; }
    if (*s == '\0') { *p = s; return NULL; }
    char *start = s;
    while (*s != '\0' && *s != ' ' && *s != '\t') { ++s; }
    if (*s != '\0') { *s = '\0'; ++s; }
    *p = s;
    return start;
}

/* Case-insensitive equality on null-terminated strings. */
static bool ci_eq(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        char ca = (*a >= 'a' && *a <= 'z') ? (char)(*a - 32) : *a;
        char cb = (*b >= 'a' && *b <= 'z') ? (char)(*b - 32) : *b;
        if (ca != cb) { return false; }
        ++a; ++b;
    }
    return *a == '\0' && *b == '\0';
}

/* =========================================================================
 * Command handlers
 * ========================================================================= */

static void cmd_help(void)
{
    uart_puts("Commands:\r\n");
    uart_puts("  HELP                       this list\r\n");
    uart_puts("  INFO                       board + KTOS info\r\n");
    uart_puts("  LED ON | OFF | TOGGLE      drive D13\r\n");
    uart_puts("  ADC                        read A0 once\r\n");
    uart_puts("  BTN                        read D2 once\r\n");
    uart_puts("  ECHO <text>                echo the rest of the line\r\n");
}

static void cmd_info(void)
{
    uart_puts("Board   : Arduino Uno R3\r\n");
    uart_puts("MCU     : ATmega328P @ 16 MHz\r\n");
    uart_puts("OS      : KTOS (cooperative, Timer1 1 ms tick)\r\n");
    uart_puts("SRAM    : 2 KB     Flash: 32 KB     EEPROM: 1 KB\r\n");
    uart_puts("Bridge  : ATmega16U2 USB CDC <-> USART0\r\n");
    uart_puts("LED D13 : ");
    uart_puts(led_is_on() ? "ON\r\n" : "OFF\r\n");
    uart_puts("BTN D2  : ");
    uart_puts(btn_pressed() ? "PRESSED\r\n" : "released\r\n");
}

static void cmd_led(const char *arg)
{
    if (arg == NULL) {
        uart_puts("Usage: LED ON | OFF | TOGGLE\r\n");
        return;
    }
    if (ci_eq(arg, "ON")) {
        led_on();      uart_puts("LED ON\r\n");
    } else if (ci_eq(arg, "OFF")) {
        led_off();     uart_puts("LED OFF\r\n");
    } else if (ci_eq(arg, "TOGGLE")) {
        led_toggle();
        uart_puts(led_is_on() ? "LED ON\r\n" : "LED OFF\r\n");
    } else {
        uart_puts("Unknown LED arg. Use ON, OFF or TOGGLE.\r\n");
    }
}

static void cmd_adc(void)
{
    uint16_t raw = adc_read(0);
    uint16_t mv  = (uint16_t)(((uint32_t)raw * 5000UL) / 1023UL);
    uart_puts("A0 raw=");
    uart_putu16(raw);
    uart_puts(" mv=");
    uart_putu16(mv);
    uart_puts("\r\n");
}

static void cmd_btn(void)
{
    uart_puts(btn_pressed() ? "Button: PRESSED\r\n" : "Button: released\r\n");
}

static void cmd_echo(const char *rest)
{
    if (rest == NULL || *rest == '\0') {
        uart_puts("\r\n");
        return;
    }
    uart_puts(rest);
    uart_puts("\r\n");
}

/* =========================================================================
 * cmd_task - the shell
 * ========================================================================= */

static WORD cmd_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam;
    (void)lParam;

    if (MsgType == KTOS_MSG_TYPE_INIT) {
        uart_puts("=============================\r\n");
        uart_puts("  KTOS USB-Serial CLI\r\n");
        uart_puts("=============================\r\n");
        uart_puts("Type HELP for commands.\r\n");
        return KTOS_MSG_SLEEP_INDEFINITLY;
    }

    if (MsgType != MSG_LINE_READY) {
        return KTOS_MSG_SLEEP_INDEFINITLY;
    }

    char *p = g_line_buf;
    char *cmd = next_token(&p);
    if (cmd == NULL) { return KTOS_MSG_SLEEP_INDEFINITLY; }

    if      (ci_eq(cmd, "HELP")) { cmd_help(); }
    else if (ci_eq(cmd, "INFO")) { cmd_info(); }
    else if (ci_eq(cmd, "LED"))  { cmd_led(next_token(&p)); }
    else if (ci_eq(cmd, "ADC"))  { cmd_adc(); }
    else if (ci_eq(cmd, "BTN"))  { cmd_btn(); }
    else if (ci_eq(cmd, "ECHO")) {
        /* Pass the unparsed remainder so multi-word echo works. */
        while (*p == ' ' || *p == '\t') { ++p; }
        cmd_echo(p);
    } else {
        uart_puts("Unknown: ");
        uart_puts(cmd);
        uart_puts("\r\nType HELP for commands.\r\n");
    }

    return KTOS_MSG_SLEEP_INDEFINITLY;
}

/* =========================================================================
 * rx_task - drains USART0, echoes, assembles a line
 * ========================================================================= */

static WORD rx_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam;
    (void)lParam;

    if (MsgType == KTOS_MSG_TYPE_INIT) {
        g_line_len = 0;
        return RX_POLL_MS;
    }

    int16_t b;
    while ((b = uart_get_byte()) >= 0) {
        char c = (char)b;

        /* Echo immediately for a responsive feel. */
        uart_putc(c);
        if (c == '\r') { uart_putc('\n'); }

        if (c == '\r' || c == '\n') {
            if (g_line_len > 0) {
                g_line_buf[g_line_len] = '\0';
                ktos_SendMsg(g_cmd_task, MSG_LINE_READY,
                             (WORD)g_line_len, 0);
                g_line_len = 0;
            }
            continue;
        }

        if (g_line_len < (LINE_BUF_SIZE - 1)) {
            g_line_buf[g_line_len++] = c;
        }
        /* Overflow: silently drop the rest of the line until newline. */
    }

    return RX_POLL_MS;
}

/* =========================================================================
 * Entry point
 * ========================================================================= */

int main(void)
{
    uart_init();
    led_init();
    btn_init();
    adc_init();

    /* cmd_task first so rx_task has a valid handle to send to. */
    g_cmd_task = ktos_InitTask(cmd_task,
                               /* StackSize = */ 96,
                               /* QueueSize = */ 4,
                               /* TaskID    = */ 'C');

    ktos_InitTask(rx_task,
                  /* StackSize = */ 64,
                  /* QueueSize = */ 2,
                  /* TaskID    = */ 'R');

    ktos_RunOS();    /* never returns */
    return 0;        /* unreachable */
}
