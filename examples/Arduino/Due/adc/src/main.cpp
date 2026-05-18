/*
 * KTOS - Arduino Due ADC example (bare-metal application code)
 *
 * A single KTOS task drives the whole program:
 *
 *   KTOS_MSG_TYPE_INIT   -> print banner
 *   KTOS_MSG_TYPE_TIMER  -> read ADC channel 7 (Arduino label A0),
 *                           print raw + millivolts, sleep 1000 ms
 *
 * The Arduino Due is a 3.3 V board with a 12-bit ADC.  Channel 7
 * maps to pin A0 (PA16 / AD7 on the SAM3X8E datasheet).  Voltage is
 * reported in millivolts and formatted manually to avoid pulling in
 * floating-point printf.
 *
 * Serial output goes through the on-board ATmega16U2 USB-serial bridge
 * via the Programming Port UART (PA8 = URXD, PA9 = UTXD).
 *
 * About the framework choice:
 *   PlatformIO's atmelsam platform does not support a "no framework"
 *   build for the Due, so we depend on Arduino *only* for boot
 *   scaffolding (vector table, PLL clock setup to 84 MHz, linker
 *   script).  This file never calls any Arduino API - no Serial,
 *   no analogRead, no pinMode.  setup() initialises peripherals and
 *   hands the CPU to ktos_RunOS() which never returns; loop() is
 *   provided only because the linker requires it.
 */

#include "sam.h"        /* SAM3X8E device header (from the atmelsam package) */
#include <stdint.h>

extern "C" {
#include "../../../../../core/ktos.h"
}

/* =========================================================================
 * Programming Port UART - 115200 8N1 at 84 MHz MCK
 *
 * Pins: PA8 = URXD, PA9 = UTXD (peripheral A on PIOA).
 * Baud divisor: BRGR = MCK / (16 * baud) = 84 000 000 / 1 843 200 = 45.57
 * Use 46 -> actual 114 130 baud, 0.93% slow (within USB-serial tolerance).
 * ========================================================================= */

static void uart_init(void)
{
    PMC->PMC_PCER0 = (1u << ID_UART);

    PIOA->PIO_ABSR &= ~(PIO_PA8 | PIO_PA9);
    PIOA->PIO_PDR   =  (PIO_PA8 | PIO_PA9);

    UART->UART_CR   = UART_CR_RSTRX | UART_CR_RSTTX | UART_CR_RXDIS | UART_CR_TXDIS;
    UART->UART_MR   = UART_MR_PAR_NO | UART_MR_CHMODE_NORMAL;
    UART->UART_PTCR = UART_PTCR_RXTDIS | UART_PTCR_TXTDIS;
    UART->UART_IDR  = 0xFFFFFFFFu;
    UART->UART_BRGR = 46;
    UART->UART_CR   = UART_CR_RXEN | UART_CR_TXEN;
}

static void uart_putc(char c)
{
    while (!(UART->UART_SR & UART_SR_TXRDY)) { }
    UART->UART_THR = (uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s) { uart_putc(*s++); }
}

static void uart_putu16(uint16_t n)
{
    char buf[6];
    uint8_t i = 0;
    if (n == 0) { uart_putc('0'); return; }
    while (n) { buf[i++] = (char)('0' + (n % 10)); n /= 10; }
    while (i--) { uart_putc(buf[i]); }
}

/* Print millivolts (0..3300) as "X.XXV". */
static void uart_put_mv_as_volts(uint16_t mv)
{
    uart_putu16(mv / 1000);
    uart_putc('.');
    uint16_t hundredths = (mv % 1000) / 10;
    if (hundredths < 10) { uart_putc('0'); }
    uart_putu16(hundredths);
    uart_putc('V');
}

/* =========================================================================
 * ADC - 12-bit, channel 7 (Arduino A0 / PA16 / AD7), 3.3 V reference
 * ========================================================================= */

static void adc_init(void)
{
    PMC->PMC_PCER1 = (1u << (ID_ADC - 32));
    ADC->ADC_CR    = ADC_CR_SWRST;
    ADC->ADC_MR    = ADC_MR_PRESCAL(2) | ADC_MR_STARTUP_SUT512;
    ADC->ADC_CHER  = ADC_CHER_CH7;
}

static uint16_t adc_read(uint8_t channel)
{
    ADC->ADC_CHDR = 0xFFFFu;
    ADC->ADC_CHER = (1u << channel);
    ADC->ADC_CR   = ADC_CR_START;
    while (!(ADC->ADC_ISR & ADC_ISR_DRDY)) { }
    return (uint16_t)(ADC->ADC_LCDR & 0x0FFFu);
}

/* =========================================================================
 * Required KTOS platform callbacks
 * ========================================================================= */

extern "C" __attribute__((noreturn))
void ktos_Emergency(const char *msg)
{
    uart_puts("\r\n[KTOS FATAL] ");
    uart_puts(msg);
    uart_puts("\r\n");
    while (1) { }
}

extern "C" void ktos_DebugPrintf(const char *fmt, ...) { (void)fmt; }
extern "C" void ktos_InitSys(void) { }

/* =========================================================================
 * KTOS 1 ms tick - TC0 channel 0, set up by the SAM3X8E BSP.
 *
 * The Arduino-SAM core already owns SysTick (for millis()), so the
 * SAM3X8E BSP drives the KTOS tick from TC0 instead.  We must read
 * TC_SR inside the ISR to clear the RC compare flag, otherwise the
 * interrupt re-fires immediately.
 * ========================================================================= */

extern "C" void ktos_timer_irq_handler(void);

extern "C" void TC0_Handler(void)
{
    (void)TC0->TC_CHANNEL[0].TC_SR;     /* clear the compare flag */
    ktos_timer_irq_handler();
}

/* =========================================================================
 * ADC task
 * ========================================================================= */

#define ADC_CHANNEL_A0   7u
#define ADC_REF_MV       3300u
#define ADC_MAX_COUNTS   4095u
#define SAMPLE_PERIOD_MS 1000u

static WORD adc_task(WORD MsgType, WORD sParam, LONG lParam)
{
    (void)sParam;
    (void)lParam;

    if (MsgType == KTOS_MSG_TYPE_INIT) {
        uart_puts("=============================\r\n");
        uart_puts("  KTOS ADC Example (Arduino Due)\r\n");
        uart_puts("=============================\r\n");
        uart_puts("Reading A0 every 1 s...\r\n");
    }

    uint16_t raw = adc_read(ADC_CHANNEL_A0);
    uint16_t mv  = (uint16_t)(((uint32_t)raw * ADC_REF_MV) / ADC_MAX_COUNTS);

    uart_puts("A0 raw=");
    uart_putu16(raw);
    uart_puts(" voltage=");
    uart_put_mv_as_volts(mv);
    uart_puts("\r\n");

    return SAMPLE_PERIOD_MS;
}

/* =========================================================================
 * Arduino entry points - setup() does the real work, loop() is dead code.
 *
 * extern "C" so Arduino's main.cpp (C linkage) can resolve them.
 * ========================================================================= */

extern "C" void setup(void)
{
    /* Disable the SAM3X8E watchdog (on by default at reset). */
    WDT->WDT_MR = WDT_MR_WDDIS;

    uart_init();
    adc_init();

    /* 256 32-bit words = 1 KB task stack — comfortable for the
     * Cortex-M3 exception frame (32 B) + KTOS R4-R11 save (32 B) +
     * UART printing chain.  The Due has 96 KB SRAM total. */
    ktos_InitTask(adc_task,
                  /* StackSize = */ 256,
                  /* QueueSize = */ 4,
                  /* TaskID    = */ 'A');

    ktos_RunOS();    /* never returns */
}

extern "C" void loop(void)
{
    /* Unreachable - KTOS owns the CPU after ktos_RunOS(). */
}
