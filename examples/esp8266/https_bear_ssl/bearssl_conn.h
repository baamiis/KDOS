#ifndef BEARSSL_CONN_H
#define BEARSSL_CONN_H

#include "c_types.h"
#include "ip_addr.h"
#include "espconn.h"
#include "lib/bearssl_inc/bearssl.h"
#include <stdint.h>

/* -------------------------------------------------------------------------
 * BearSSL / espconn integration
 *
 * Call bearssl_init() from the espconn connect callback to start the TLS
 * handshake.  Wire the remaining three functions to the espconn callbacks:
 *
 *   espconn_regist_recvcb   → bearssl_on_recv
 *   espconn_regist_sentcb   → bearssl_on_sent
 *   espconn_regist_reconcb  → bearssl_on_error
 *   espconn_regist_disconcb → bearssl_on_discon
 * ------------------------------------------------------------------------- */

/* BearSSL internal TLS record I/O buffer (half-duplex).
 * Must fit the largest TLS record the server sends.
 * httpbin.org Certificate message is ~4 KB; 6144 gives comfortable headroom. */
#define BEAR_IOBUF_SIZE  6144

/* Ring buffer for TCP bytes received from espconn before BearSSL drains them.
 * The server's Certificate burst (~4 KB) arrives while ClientHello is in
 * flight (g_sending=true), so the ring buffer must absorb the full burst. */
#define BEAR_RXBUF_SIZE  4096

void bearssl_init    (struct espconn *conn, const char *hostname);
void bearssl_on_recv (void *arg, char *data, uint16_t len);
void bearssl_on_sent (void *arg);
void bearssl_on_error(void *arg, sint8 err);
void bearssl_on_discon(void *arg);

#endif /* BEARSSL_CONN_H */
