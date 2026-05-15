/**
 * @file bearssl_conn.c
 * @brief BearSSL TLS 1.2 client over espconn for ESP8266 Non-OS SDK.
 *
 * BearSSL uses a push-model state machine: the caller queries what the engine
 * needs (send to network / receive from network / send app data / recv app data)
 * and services each request.  The Non-OS SDK is callback-driven with no blocking
 * I/O, so we adapt by:
 *
 *  - Buffering incoming TCP bytes in a ring buffer (espconn recv callback).
 *  - Running the BearSSL state machine (bear_drive) after every espconn event.
 *  - Sending only one espconn_sent at a time; waiting for sentcb before the next.
 *
 * Half-duplex mode (single I/O buffer) is used so BR_SSL_SENDREC and
 * BR_SSL_RECVREC are always mutually exclusive — no two-buffer coordination
 * needed, which saves ~8 KB of RAM.
 */

#include "bearssl_conn.h"
#include "bearssl.h"
#include "espconn.h"
#include <stdlib.h>
#include "http_config.h"
#include "trust_anchors.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "osapi.h"

/* Response globals shared with main.c — defined in startup.c */
extern char     g_resp_buf[];
extern uint16_t g_resp_len;
extern uint8_t  g_resp_err;
extern int      g_ssl_err;

/* Permanent exit to KTOS — defined in startup.c */
extern void do_exit(void);

/* Pre-built HTTP request */
static const char g_request[] =
    "GET " HTTP_PATH " HTTP/1.1\r\n"
    "Host: " HTTP_HOST "\r\n"
    "Connection: close\r\n"
    "User-Agent: KTOS/ESP8266-BearSSL\r\n"
    "\r\n";

/* =========================================================================
 * BearSSL state
 * ========================================================================= */

static br_ssl_client_context   g_sc;
static br_x509_minimal_context g_xc;
static unsigned char          *g_iobuf;  /* heap-allocated in bearssl_init() */

/* =========================================================================
 * Ring buffer — holds raw TCP bytes between espconn recv and BearSSL
 * ========================================================================= */

static uint8_t  *g_rxbuf;  /* heap-allocated in bearssl_init() */
static uint16_t  g_rxhead;
static uint16_t  g_rxtail;
static uint16_t  g_rxlen;

static uint16_t rxbuf_space(void) {
    return (uint16_t)(BEAR_RXBUF_SIZE - g_rxlen);
}

static void rxbuf_push(const uint8_t *src, uint16_t n) {
    for (uint16_t i = 0; i < n; i++) {
        g_rxbuf[g_rxtail] = src[i];
        g_rxtail = (uint16_t)((g_rxtail + 1u) % BEAR_RXBUF_SIZE);
    }
    g_rxlen = (uint16_t)(g_rxlen + n);
}

static uint16_t rxbuf_pop(uint8_t *dst, uint16_t max) {
    uint16_t n = g_rxlen < max ? g_rxlen : max;
    for (uint16_t i = 0; i < n; i++) {
        dst[i] = g_rxbuf[g_rxhead];
        g_rxhead = (uint16_t)((g_rxhead + 1u) % BEAR_RXBUF_SIZE);
    }
    g_rxlen = (uint16_t)(g_rxlen - n);
    return n;
}

/* =========================================================================
 * Internal state flags
 * ========================================================================= */

static bool     g_sending;         /* espconn_sent in flight              */
static uint16_t g_pending_ack;     /* bytes to ack in bearssl_on_sent     */
static bool     g_request_sent;    /* HTTP GET has been injected          */
static bool     g_network_closed;  /* espconn disconnect/error received   */

/* =========================================================================
 * State machine driver
 *
 * Called after every espconn event.  Runs until the engine blocks on
 * network I/O or until an espconn_sent is queued (only one in flight).
 * ========================================================================= */

static void bear_drive(struct espconn *conn)
{
    for (;;) {
        unsigned st = br_ssl_engine_current_state(&g_sc.eng);

        /* ---- Closed / fatal error ---- */
        if (st == BR_SSL_CLOSED) {
            int err = br_ssl_engine_last_error(&g_sc.eng);
            os_printf("[bear] closed err=%d resp_len=%u\n", err, (unsigned)g_resp_len);
            /* BR_ERR_OK = clean close_notify; anything else is a TLS error. */
            if (err != BR_ERR_OK && g_resp_len == 0) {
                g_resp_err = 2;
                g_ssl_err  = err;
            }
            do_exit();
            return;
        }

        /* ---- Send encrypted bytes to network ---- */
        if ((st & BR_SSL_SENDREC) && !g_sending) {
            size_t len;
            unsigned char *buf = br_ssl_engine_sendrec_buf(&g_sc.eng, &len);
            if (len > 0) {
                g_pending_ack = (uint16_t)len;
                g_sending     = true;
                os_printf("[bear] send %u st=0x%x\n", (unsigned)len, st);
                sint8 rc = espconn_sent(conn, buf, (uint16)len);
                if (rc != 0) {
                    os_printf("[bear] espconn_sent err=%d\n", (int)rc);
                    g_resp_err = 2;
                    do_exit();
                    return;
                }
                return; /* resume in bearssl_on_sent */
            }
        }

        /* ---- Inject HTTP GET when handshake is complete ---- */
        /* Must be checked before RECVREC: after the handshake BearSSL sets
         * both SENDAPP and RECVREC simultaneously.  Checking RECVREC first
         * with an empty ring buffer would cause an early return and the GET
         * would never be injected. */
        if ((st & BR_SSL_SENDAPP) && !g_request_sent) {
            size_t blen;
            unsigned char *bbuf = br_ssl_engine_sendapp_buf(&g_sc.eng, &blen);
            size_t req = sizeof(g_request) - 1u;
            os_printf("[bear] SENDAPP blen=%u req=%u\n", (unsigned)blen, (unsigned)req);
            if (blen >= req) {
                os_printf("[bear] injecting HTTP GET\n");
                memcpy(bbuf, g_request, req);
                br_ssl_engine_sendapp_ack(&g_sc.eng, req);
                br_ssl_engine_flush(&g_sc.eng, 0);
                g_request_sent = true;
                continue; /* will become SENDREC */
            }
        }

        /* ---- Feed buffered TCP bytes into BearSSL ---- */
        if (st & BR_SSL_RECVREC) {
            size_t blen;
            unsigned char *bbuf = br_ssl_engine_recvrec_buf(&g_sc.eng, &blen);
            if (g_rxlen > 0 && blen > 0) {
                uint16_t n = rxbuf_pop(bbuf, (uint16_t)blen);
                os_printf("[bear] ack %u/%u\n", n, (unsigned)blen);
                br_ssl_engine_recvrec_ack(&g_sc.eng, (size_t)n);
                continue; /* re-evaluate state */
            }
            os_printf("[bear] RECVREC wait rxlen=%u blen=%u\n", g_rxlen, (unsigned)blen);
            /* Ring buffer empty — need more TCP data. */
            if (g_network_closed) {
                /* Connection dropped before clean TLS close. */
                if (g_resp_len > 0)
                    do_exit();          /* response already buffered */
                else {
                    g_resp_err = 2;
                    do_exit();
                }
                return;
            }
            return; /* wait for bearssl_on_recv */
        }

        /* ---- Copy decrypted application data to response buffer ---- */
        if (st & BR_SSL_RECVAPP) {
            size_t blen;
            const unsigned char *bbuf = br_ssl_engine_recvapp_buf(&g_sc.eng, &blen);
            if (blen > 0) {
                uint16_t space = (uint16_t)((HTTP_RESP_BUF - 1u) - g_resp_len);
                uint16_t n = (uint16_t)blen < space ? (uint16_t)blen : space;
                if (n > 0) {
                    memcpy(g_resp_buf + g_resp_len, bbuf, n);
                    g_resp_len = (uint16_t)(g_resp_len + n);
                }
                br_ssl_engine_recvapp_ack(&g_sc.eng, blen); /* always ack all */
                continue;
            }
        }

        break; /* nothing progressed — wait for the next event */
    }
}

/* =========================================================================
 * Public API
 * ========================================================================= */

void ICACHE_FLASH_ATTR bearssl_init(struct espconn *conn, const char *hostname)
{
    /* Allocate I/O buffers from heap now that WiFi+TCP is up.
     * Keeping them out of BSS gives the WiFi stack ~10 KB more heap
     * during initialization, which prevents the "hang at phy ver" failure. */
    if (!g_iobuf) g_iobuf = (unsigned char *)malloc(BEAR_IOBUF_SIZE);
    if (!g_rxbuf) g_rxbuf = (uint8_t *)malloc(BEAR_RXBUF_SIZE);
    if (!g_iobuf || !g_rxbuf) { g_resp_err = 2; do_exit(); return; }
    os_printf("[bear] init iobuf=%p rxbuf=%p\n", g_iobuf, g_rxbuf);

    /* Full TLS 1.2 client: RSA + ECDHE, AES-GCM + ChaCha20, SHA-256/384. */
    br_ssl_client_init_full(&g_sc, &g_xc, TAs, TAs_NUM);

    /* Set current time so x509 date validation passes.
     * 2026-05-01 00:00 UTC → BearSSL days = unix_ts/86400 + 719528 = 740102.
     * Update this constant when the server cert chain changes. */
    br_x509_minimal_set_time(&g_xc, 740102u, 0u);

    /* Half-duplex single buffer — saves ~8 KB vs bidirectional mode. */
    br_ssl_engine_set_buffer(&g_sc.eng, g_iobuf, BEAR_IOBUF_SIZE, 0);

    /* Seed BEFORE reset: br_ssl_client_reset() tries the OS seeder, finds
     * none on bare-metal ESP8266, and immediately fails the engine with
     * BR_ERR_NO_RANDOM. We initialize the HMAC-DRBG from the hardware RNG
     * register (0x3FF20E44, fed by WiFi radio noise) and mark it seeded
     * so reset() skips the OS seeder path entirely. */
    {
        uint32_t hw[8];
        for (int i = 0; i < 8; i++)
            hw[i] = *(volatile uint32_t *)0x3FF20E44;
        br_hmac_drbg_init(&g_sc.eng.rng, &br_sha256_vtable, hw, sizeof hw);
        g_sc.eng.rng_init_done = 2;
    }

    br_ssl_client_reset(&g_sc, hostname, 0);

    g_rxhead = g_rxtail = g_rxlen = 0;
    g_request_sent  = false;
    g_sending       = false;
    g_pending_ack   = 0;
    g_network_closed = false;

    /* Initial engine state is BR_SSL_SENDREC (ClientHello ready to send). */
    bear_drive(conn);
}

void ICACHE_FLASH_ATTR bearssl_on_recv(void *arg, char *data, uint16_t len)
{
    struct espconn *conn = (struct espconn *)arg;

    os_printf("[bear] recv %u bytes (rxlen=%u)\n", (unsigned)len, (unsigned)g_rxlen);

    /* Cap at ring buffer space to avoid overrun. */
    uint16_t space = rxbuf_space();
    if (len > space) len = space;
    if (len > 0) rxbuf_push((const uint8_t *)data, len);

    /* Don't drive while a send is in flight — bear_drive will run in sentcb. */
    if (!g_sending)
        bear_drive(conn);
}

void ICACHE_FLASH_ATTR bearssl_on_sent(void *arg)
{
    struct espconn *conn = (struct espconn *)arg;

    os_printf("[bear] sentcb\n");
    if (g_sending) {
        /* espconn has dispatched the data; now safe to release BearSSL's buffer. */
        br_ssl_engine_sendrec_ack(&g_sc.eng, (size_t)g_pending_ack);
        g_pending_ack = 0;
        g_sending     = false;
    }

    bear_drive(conn);
}

void ICACHE_FLASH_ATTR bearssl_on_error(void *arg, sint8 err)
{
    (void)arg;
    int ssl_err = br_ssl_engine_last_error(&g_sc.eng);
    os_printf("[bear] error %d ssl_err=%d resp_len=%u\n",
              (int)err, ssl_err, (unsigned)g_resp_len);
    if (g_resp_len == 0) { g_resp_err = 2; g_ssl_err = ssl_err; }
    do_exit();
}

void ICACHE_FLASH_ATTR bearssl_on_discon(void *arg)
{
    (void)arg;
    int ssl_err = br_ssl_engine_last_error(&g_sc.eng);
    os_printf("[bear] discon resp_len=%u ssl_err=%d\n",
              (unsigned)g_resp_len, ssl_err);
    /* TCP closed. All data was already delivered via recv callbacks.
     * If g_sending is still true (close_notify in flight), sentcb won't
     * fire — calling bear_drive would stall. Exit directly instead. */
    if (g_resp_len == 0) { g_resp_err = 2; g_ssl_err = ssl_err; }
    do_exit();
}
