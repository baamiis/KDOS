#ifndef HTTP_CONFIG_H
#define HTTP_CONFIG_H

/* -------------------------------------------------------------------------
 * HTTPS endpoint — BearSSL TLS 1.2 client
 *
 * Any server that accepts TLS 1.2 with RSA or ECDHE-RSA cipher suites will
 * work.  httpbin.org is the default; its CA chain terminates at ISRG Root X1
 * (Let's Encrypt), which is provided as the default trust anchor.
 * ------------------------------------------------------------------------- */
#define HTTP_HOST  "httpbin.org"
#define HTTP_PORT  443
#define HTTP_PATH  "/ip"

/* BearSSL internal I/O buffer — see bearssl_conn.h BEAR_IOBUF_SIZE.
 * 8192 bytes handles most servers; increase to 16384 for large cert chains. */

/* Response capture — bytes beyond this limit are silently discarded. */
#define HTTP_RESP_BUF  2048

#endif
