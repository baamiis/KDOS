/*
 * KTOS - Arduino Uno EEPROM example (bare-metal)
 *
 * The ATmega328P has 1 KB of internal EEPROM that survives power
 * cycles.  This example demonstrates the canonical "boot counter"
 * pattern plus an interactive shell for inspecting and editing the
 * EEPROM contents.
 *
 * Two cooperating KTOS tasks:
 *
 *   counter_task ('E') -- on INIT: read a 16-bit boot counter from
 *                         EEPROM[0..1], increment, write it back,
 *                         and print the new value.
 *                         on TIMER (every 5 s): print uptime.
 *                         on MSG_LINE_READY: handle HELP / INFO /
 *                         READ / WRITE / DUMP commands.  The 5 s
 *                         timer simply restarts after every command.
 *
 *   rx_task      ('R') -- wakes every 10 ms, drains USART0, echoes
 *                         every byte, and on '\r' or '\n' posts
 *                         MSG_LINE_READY to counter_task.
 *
 * Power-cycle the Uno and watch the boot count rise by one each time.
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

static void uart_put_nibble(uint8_t n)
{
    n &= 0x0F;
    uart_putc((char)(n < 10 ? ('0' + n) : ('A' + n - 10)));
}

static void uart_put_hex2(uint8_t b)
{
    uart_put_nibble((uint8_t)(b >> 4));
    uart_put_nibble(b);
}

/* =========================================================================
 * EEPROM - 1 KB, addresses 0..1023
 *
 * Read: clear EERE, wait for any pending write, set EEAR, set EERE.
 *       Hardware halts the CPU for 4 cycles while the byte is fetched.
 *
 * Write: requires a strict 4-cycle sequence with interrupts disabled.
 *        After the write begins the cell is updated over ~3.4 ms.
 *        We do not block here - the next call's "wait for EEPE" handles
 *        serialisation.
 * ========================================================================= */

static inline void eeprom_wait_ready(void)
{
    while (EECR & (1 << EEPE)) { }
}

static uint8_t eeprom_read(uint16_t addr)
{
    eeprom_wait_ready();
    EEAR = addr;
    EECR |= (1 << EERE);
    return EEDR;
}

static void eeprom_write(uint16_t addr, uint8_t value)
{
    eeprom_wait_ready();
    uint8_t sreg = SREG;
    cli();
    EEAR = addr;
    EEDR = value;
    EECR |= (1 << EEMPE);
    EECR |= (1 << EEPE);    /* must be within 4 cycles of EEMPE */
    SREG = sreg;
}

static uint16_t eeprom_read16(uint16_t addr)
{
    uint16_t lo = eeprom_read(addr);
    uint16_t hi = eeprom_read((uint16_t)(addr + 1));
    return (uint16_t)(lo | (hi << 8));
}

static void eeprom_write16(uint16_t addr, uint16_t value)
{
    eeprom_write(addr,           (uint8_t)(value & 0xFF));
    eeprom_write((uint16_t)(addr + 1), (uint8_t)((value >> 8) & 0xFF));
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
 * Application config + shared state
 * ========================================================================= */

#define MSG_LINE_READY            ((WORD)(KTOS_MSG_TYPE_SYSTEM_START + 1))
#define EEPROM_SIZE_BYTES         1024U
#define EEPROM_BOOT_COUNTER_ADDR  0U      /* 2 bytes at 0..1 */
#define HEARTBEAT_PERIOD_MS       5000U
#define RX_POLL_MS                10U
#define LINE_BUF_SIZE             40U     /* room for "WRITE 1023 255" */

static char    g_line_buf[LINE_BUF_SIZE];
static uint8_t g_line_len = 0;

static uint16_t g_boot_count = 0;
static uint16_t g_uptime_s   = 0;

static struct ktos_TASK *g_counter_task = NULL;

/* =========================================================================
 * Tokeniser + small string helpers
 * ========================================================================= */

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

/* Parse decimal uint16_t from a null-terminated string.  Stops at the
 * first non-digit.  Returns false on empty input or overflow >65535. */
static bool parse_u16(const char *s, uint16_t *out)
{
    if (s == NULL || *s == '\0') { return false; }
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10U + (uint32_t)(*s - '0');
        if (v > 65535UL) { return false; }
        ++s;
    }
    if (*s != '\0') { return false; }    /* extra garbage after digits */
    *out = (uint16_t)v;
    return true;
}

/* =========================================================================
 * Command handlers
 * ========================================================================= */

static void cmd_help(void)
{
    uart_puts("Commands:\r\n");
    uart_puts("  HELP                this list\r\n");
    uart_puts("  INFO                boot count + uptime\r\n");
    uart_puts("  READ <addr>         read one byte (addr 0..1023)\r\n");
    uart_puts("  WRITE <addr> <val>  write one byte (val 0..255)\r\n");
    uart_puts("  DUMP                show first 64 EEPROM bytes\r\n");
}

static void print_status(void)
{
    uart_puts("Boot #");
    uart_putu16(g_boot_count);
    uart_puts(", uptime ");
    uart_putu16(g_uptime_s);
    uart_puts(" s\r\n");
}

static void cmd_read(const char *addr_str)
{
    uint16_t addr;
    if (!parse_u16(addr_str, &addr) || addr >= EEPROM_SIZE_BYTES) {
        uart_puts("Bad addr (use 0..1023)\r\n");
        return;
    }
    uint8_t v = eeprom_read(addr);
    uart_puts("EEPROM[");
    uart_putu16(addr);
    uart_puts("] = ");
    uart_putu16(v);
    uart_puts(" (0x");
    uart_put_hex2(v);
    uart_puts(")\r\n");
}

static void cmd_write(const char *addr_str, const char *val_str)
{
    uint16_t addr, val;
    if (!parse_u16(addr_str, &addr) || addr >= EEPROM_SIZE_BYTES) {
        uart_puts("Bad addr (use 0..1023)\r\n");
        return;
    }
    if (!parse_u16(val_str, &val) || val > 255U) {
        uart_puts("Bad value (use 0..255)\r\n");
        return;
    }
    eeprom_write(addr, (uint8_t)val);
    uart_puts("Wrote EEPROM[");
    uart_putu16(addr);
    uart_puts("] = ");
    uart_putu16(val);
    uart_puts("\r\n");
}

static void cmd_dump(void)
{
    uart_puts("EEPROM 0..63:\r\n");
    for (uint16_t row = 0; row < 4U; ++row) {
        uart_put_hex2((uint8_t)(row * 16U));
        uart_puts(": ");
        for (uint16_t col = 0; col < 16U; ++col) {
            uint8_t v = eeprom_read((uint16_t)(row * 16U + col));
            uart_put_hex2(v);
            uart_putc(' ');
        }
        uart_puts("\r\n");
    }
}

/* =========================================================================
 * counter_task - owns EEPROM, the boot counter, and command dispatch
 * ========================================================================= */

static WORD counter_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam;
    (void)lParam;

    if (MsgType == KTOS_MSG_TYPE_INIT) {
        uint16_t cnt = eeprom_read16(EEPROM_BOOT_COUNTER_ADDR);
        if (cnt == 0xFFFFU) { cnt = 0; }     /* factory-blank cell */
        ++cnt;
        eeprom_write16(EEPROM_BOOT_COUNTER_ADDR, cnt);
        g_boot_count = cnt;
        g_uptime_s   = 0;

        uart_puts("=============================\r\n");
        uart_puts("  KTOS EEPROM Example\r\n");
        uart_puts("=============================\r\n");
        print_status();
        uart_puts("Type HELP for commands.\r\n");
        return HEARTBEAT_PERIOD_MS;
    }

    if (MsgType == KTOS_MSG_TYPE_TIMER) {
        g_uptime_s = (uint16_t)(g_uptime_s + 5U);
        print_status();
        return HEARTBEAT_PERIOD_MS;
    }

    if (MsgType == MSG_LINE_READY) {
        char *p   = g_line_buf;
        char *cmd = next_token(&p);
        if (cmd == NULL) { return HEARTBEAT_PERIOD_MS; }

        if      (ci_eq(cmd, "HELP")) { cmd_help(); }
        else if (ci_eq(cmd, "INFO")) { print_status(); }
        else if (ci_eq(cmd, "READ")) { cmd_read(next_token(&p)); }
        else if (ci_eq(cmd, "WRITE")) {
            char *a = next_token(&p);
            char *v = next_token(&p);
            cmd_write(a, v);
        }
        else if (ci_eq(cmd, "DUMP")) { cmd_dump(); }
        else {
            uart_puts("Unknown: ");
            uart_puts(cmd);
            uart_puts("\r\nType HELP for commands.\r\n");
        }

        /* Heartbeat clock restarts from this point - that's fine. */
        return HEARTBEAT_PERIOD_MS;
    }

    return HEARTBEAT_PERIOD_MS;
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

        uart_putc(c);
        if (c == '\r') { uart_putc('\n'); }

        if (c == '\r' || c == '\n') {
            if (g_line_len > 0) {
                g_line_buf[g_line_len] = '\0';
                ktos_SendMsg(g_counter_task, MSG_LINE_READY,
                             (WORD)g_line_len, 0);
                g_line_len = 0;
            }
            continue;
        }

        if (g_line_len < (LINE_BUF_SIZE - 1)) {
            g_line_buf[g_line_len++] = c;
        }
        /* Overflow: silently drop the rest of the line. */
    }

    return RX_POLL_MS;
}

/* =========================================================================
 * Entry point
 * ========================================================================= */

int main(void)
{
    uart_init();

    /* counter_task first so rx_task has a valid handle to send to. */
    g_counter_task = ktos_InitTask(counter_task,
                                   /* StackSize = */ 96,
                                   /* QueueSize = */ 4,
                                   /* TaskID    = */ 'E');

    ktos_InitTask(rx_task,
                  /* StackSize = */ 64,
                  /* QueueSize = */ 2,
                  /* TaskID    = */ 'R');

    ktos_RunOS();    /* never returns */
    return 0;        /* unreachable */
}
