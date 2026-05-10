# HTTP(S) GET Example {#example_https_get}

Makes an HTTP or HTTPS GET request from the ESP8266 using the ESP8266
Non-OS SDK's `espconn` / axTLS stack and prints the response status line
and body via UART.

HTTPS works here because KTOS's small footprint frees approximately 36 KB of
heap compared with a typical Arduino sketch, giving axTLS enough room to
complete the TLS handshake — something that is not reliably possible on
standard Arduino.

---

## What It Does

1. `startup.c` connects to WiFi in `STATION_MODE`.
2. On `EVENT_STAMODE_GOT_IP`: optionally syncs the clock via SNTP (when
   `HTTP_SNTP_SYNC 1`), then resolves `HTTP_HOST` via DNS.
3. `dns_done_cb()` sets up an `espconn` TCP (or TLS) connection, optionally
   enabling CA verification and/or a client certificate from flash.
4. `http_connect_cb()` sends the HTTP GET request.
5. `http_recv_cb()` accumulates the response into `g_resp_buf`.
6. `http_discon_cb()` stops FRC1, disables watchdogs, and crosses into CALL0
   via `callx0` → `ktos_app_main()`.
7. `http_task` parses the response and prints the status line and body.

---

## Prerequisites

| Tool | Notes |
|------|-------|
| `xtensa-lx106-elf-gcc` | Xtensa toolchain for LX106 |
| `esptool.py` | Flash utility |
| ESP8266 Non-OS SDK | Expected at `~/esp/sdk` (override with `SDK_DIR=`) |
| Python 3 | Required only for `cert_to_flash.py` (certificate flashing) |

Serial monitor set to **74880 baud, 8N1**.

---

## Configuration

### WiFi credentials — `wifi_config.h`

```c
#define WIFI_SSID     "YourNetwork"
#define WIFI_PASSWORD "YourPassword"
```

### HTTP endpoint — `http_config.h`

```c
#define HTTP_USE_SSL  1           /* 0 = HTTP, 1 = HTTPS (axTLS)          */
#define HTTP_PORT     443         /* 80 for HTTP, 443 for HTTPS            */
#define HTTP_HOST     "httpbin.org"
#define HTTP_PATH     "/ip"
#define HTTP_SSL_BUF  4096        /* axTLS record buffer — increase if handshake fails */
```

### Certificate authentication — `http_config.h`

All certificate options require `HTTP_USE_SSL 1`.

```c
/* Server CA verification — axTLS checks the server cert chain */
#define HTTP_VERIFY_CA           0    /* 1 = verify server CA from flash   */
#define HTTP_CA_FLASH_SECTOR     0xEC /* flash address 0x0EC000            */

/* Client certificate — mutual TLS */
#define HTTP_CLIENT_CERT         0    /* 1 = send client cert              */
#define HTTP_CLIENT_FLASH_SECTOR 0xED /* flash address 0x0ED000            */

/* SNTP clock sync — required when HTTP_VERIFY_CA 1 and you care about
   certificate expiry.  Leave 0 when using a root CA or skipping expiry. */
#define HTTP_SNTP_SYNC           0    /* 1 = sync clock before TLS         */
#define HTTP_SNTP_SERVER         "pool.ntp.org"
#define HTTP_SNTP_TIMEOUT_MS     15000
```

| `HTTP_VERIFY_CA` | `HTTP_SNTP_SYNC` | Behaviour |
|:---:|:---:|---|
| 0 | — | No cert check, straight to DNS |
| 1 | 0 | CA chain verified, expiry not checked (good for root CAs) |
| 1 | 1 | Full validation including notBefore / notAfter |

---

## Build

```bash
cd examples/esp8266/https_get
make
```

Produces:

```
https_get-0x00000.bin   (IRAM image)
https_get-0x10000.bin   (IROM / XIP image)
```

---

## Flash

### Firmware

```bash
make flash PORT=/dev/ttyUSB0
```

Full flash map:

| Address    | File |
|------------|------|
| `0x00000`  | `https_get-0x00000.bin` |
| `0x10000`  | `https_get-0x10000.bin` |
| `0x3FB000` | `blank.bin` (RF_CAL) |
| `0x3FC000` | `esp_init_data_default_v08.bin` (PHY init) |
| `0x3FD000` | `blank.bin` (system params) |

### CA certificate (when `HTTP_VERIFY_CA 1`)

Convert a PEM certificate to the axTLS flash image format and write it to
flash sector `0xEC` (address `0x0EC000`):

```bash
make flash_ca_cert PORT=/dev/ttyUSB0 CA_PEM=ca.pem
```

This calls `cert_to_flash.py ca ca.pem ca_flash.bin` internally.  The flash
image format is:

```
[uint16_t cert_len, little-endian] [DER cert data] [0xFF pad to 4096 bytes]
```

### Client certificate + private key (when `HTTP_CLIENT_CERT 1`)

```bash
make flash_client_cert PORT=/dev/ttyUSB0 CLIENT_PEM=client.pem CLIENT_KEY=client_key.pem
```

The flash image format for the combined sector is:

```
[uint16_t cert_len] [DER cert] [uint16_t key_len] [DER key] [0xFF pad to 4096 bytes]
```

Both cert and key must be in PEM format.  `cert_to_flash.py` converts them
to DER automatically.

---

## Expected Output

### HTTP mode (`HTTP_USE_SSL 0`)

```
=============================
   KTOS HTTP GET
=============================
Host   : httpbin.org
Path   : /ip
Mode   : HTTP
----
Status : HTTP/1.1 200 OK
Body   :
{
  "origin": "203.0.113.42"
}
----
Done. (256 bytes total)
```

### HTTPS mode (`HTTP_USE_SSL 1`)

```
=============================
   KTOS HTTPS GET
=============================
Host   : httpbin.org
Path   : /ip
Mode   : HTTPS (axTLS)
----
Status : HTTP/1.1 200 OK
Body   :
{
  "origin": "203.0.113.42"
}
----
Done. (256 bytes total)
```

### HTTPS with CA verification and SNTP

```
Mode   : HTTPS (axTLS, CA verify)
```

### Error cases

| `g_resp_err` | Message | Cause |
|:---:|---|---|
| 1 | DNS resolution failed | `HTTP_HOST` unreachable or DNS down |
| 2 | TLS/TCP connection failed | Server requires TLS 1.2+ or ECDHE |
| 3 | SNTP time sync timed out | NTP unreachable; increase `HTTP_SNTP_TIMEOUT_MS` |

---

## How It Works

### axTLS constraints

The ESP8266 Non-OS SDK bundles axTLS, which only supports TLS 1.0/1.1 and
RSA key-exchange cipher suites.  Servers that require TLS 1.2 or ECDHE
(e.g. most modern CDN-backed hosts) will reject the connection.
`httpbin.org` accepts the axTLS handshake.

### Heap advantage

A typical Arduino sketch for the ESP8266 uses approximately 36 KB of heap for
its runtime before any user code runs.  KTOS uses roughly 4 KB.  This freed
heap is what allows axTLS to complete the TLS handshake, which was otherwise
impossible on unmodified Arduino sketches.

### SNTP and certificate time validation

axTLS validates the server certificate's `notBefore` and `notAfter` fields
against the current system time.  The ESP8266 has no real-time clock; after
a power cycle the time is at Unix epoch (1970-01-01).  Setting
`HTTP_SNTP_SYNC 1` polls `pool.ntp.org` via UDP port 123 inside an
`os_timer` callback after getting an IP address.  Once
`sntp_get_current_timestamp()` returns a non-zero value the timer is disarmed
and the normal DNS → TLS → GET flow proceeds.

Setting `HTTP_SNTP_SYNC 0` while `HTTP_VERIFY_CA 1` still performs CA chain
verification — it just skips expiry checking.  This is appropriate when your
server uses a well-known root CA and you trust the chain but do not need
strict expiry enforcement.

### Flash certificate layout

Both certificate sectors sit in the free flash gap between OTA2 end
(`0x0EB000`) and RF_CAL (`0x3FB000`).  They are registered as
`SYSTEM_PARTITION_SSL_CLIENT_CA` and `SYSTEM_PARTITION_SSL_CLIENT_CERT_PRIVKEY`
in the partition table, which is what `espconn_secure_ca_enable()` and
`espconn_secure_cert_req_enable()` expect.
