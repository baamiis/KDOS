#ifndef HTTP_CONFIG_H
#define HTTP_CONFIG_H

/* -------------------------------------------------------------------------
 * HTTP / HTTPS endpoint configuration
 *
 * HTTP_USE_SSL 0  — plain HTTP  (always works, good starting point)
 * HTTP_USE_SSL 1  — HTTPS       (requires server TLS 1.0/1.1 + RSA)
 *
 * The ESP8266 Non-OS SDK bundles axTLS which only supports TLS 1.0/1.1 and
 * RSA key-exchange cipher suites.  Modern servers that require TLS 1.2 or
 * ECDHE will reject the connection.  The memory freed by KTOS (~36 KB vs
 * Arduino) gives axTLS enough heap to complete the handshake — something
 * that is not possible on a standard Arduino sketch.
 *
 * SSL buffer: set to 4096 bytes via espconn_secure_set_size() to cap
 * memory use.  The default (5 KB) is safe to increase if handshakes fail.
 *
 * Default endpoint: httpbin.org /ip  — returns {"origin":"<your-ip>"}
 * Change HTTP_USE_SSL to 1 and set HTTP_PORT to 443 to attempt HTTPS.
 * ------------------------------------------------------------------------- */
#define HTTP_USE_SSL  1
#define HTTP_PORT     443
//#define HTTP_USE_SSL  0               /* 0 = HTTP, 1 = HTTPS (axTLS)        */
#define HTTP_HOST     "httpbin.org"
//#define HTTP_PORT     80              /* 80 for HTTP, 443 for HTTPS          */
#define HTTP_PATH     "/ip"
#define HTTP_SSL_BUF  4096            /* axTLS record buffer size in bytes   */

/* -------------------------------------------------------------------------
 * Certificate authentication (requires HTTP_USE_SSL 1)
 *
 * HTTP_VERIFY_CA 1   — verify server certificate against CA in flash.
 *                      Flash sector HTTP_CA_FLASH_SECTOR must contain the
 *                      CA cert image written by cert_to_flash.py.
 *
 * HTTP_CLIENT_CERT 1 — send a client certificate (mutual TLS).
 *                      Flash sector HTTP_CLIENT_FLASH_SECTOR must contain
 *                      the combined client cert + private key image.
 *
 * Both default to 0 (disabled) so the example builds with no certs present.
 * ------------------------------------------------------------------------- */
#define HTTP_VERIFY_CA           0    /* 1 = verify server CA from flash     */
#define HTTP_CLIENT_CERT         0    /* 1 = send client cert (mutual TLS)   */
#define HTTP_CA_FLASH_SECTOR     0xEC /* 0x0EC000 — CA cert for server verify */
#define HTTP_CLIENT_FLASH_SECTOR 0xED /* 0x0ED000 — client cert + privkey    */

/* -------------------------------------------------------------------------
 * SNTP (optional — set HTTP_SNTP_SYNC 1 when HTTP_VERIFY_CA 1)
 *
 * axTLS validates the server certificate's notBefore/notAfter fields, so
 * it needs the current time.  Set HTTP_SNTP_SYNC 1 to sync the clock via
 * NTP before the TLS handshake.
 *
 * Leave HTTP_SNTP_SYNC 0 if your CA is a root cert or you don't care about
 * expiry checking — axTLS will still verify the cert chain, it just won't
 * reject an expired certificate.
 *
 * HTTP_SNTP_TIMEOUT_MS — how long to wait for an NTP response before
 * aborting with g_resp_err = 3.
 * ------------------------------------------------------------------------- */
#define HTTP_SNTP_SYNC        0       /* 1 = sync clock via NTP before TLS  */
#define HTTP_SNTP_SERVER      "pool.ntp.org"
#define HTTP_SNTP_TIMEOUT_MS  15000   /* 15 s — abort if clock not synced   */

/* Response capture — bytes beyond this limit are silently discarded. */
#define HTTP_RESP_BUF 2048

#endif
