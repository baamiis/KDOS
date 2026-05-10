# HTTPS GET with BearSSL TLS 1.2 {#example_https_bear_ssl}

Makes an HTTPS GET request using **BearSSL** — a TLS 1.2 library designed for
constrained embedded systems — instead of the ESP8266 SDK's bundled axTLS.

This example is the key KTOS differentiator: the freed heap (~26 KB over a
typical Arduino sketch) makes room for BearSSL's working state, enabling
connections to modern servers that have already retired TLS 1.0/1.1.

---

## Quick start

1. Edit `wifi_config.h`:
   ```c
   #define WIFI_SSID     "YourNetwork"
   #define WIFI_PASSWORD "YourPassword"
   ```

2. Edit `http_config.h` if targeting a different server (default: `httpbin.org/ip`).

3. Build and flash:
   ```bash
   make
   make flash PORT=/dev/ttyUSB0
   ```

4. Open a serial monitor at **74880 8N1** and observe the output.

---

## Prerequisites

| Tool                   | Notes                                           |
|------------------------|-------------------------------------------------|
| `xtensa-lx106-elf-gcc` | Xtensa toolchain for ESP8266 (LX106 core)       |
| `esptool.py`           | Firmware flashing utility                       |
| ESP8266 Non-OS SDK     | Default path `~/esp/sdk`; override with `SDK_DIR=` |

BearSSL is pre-built and committed as `lib/libbearssl.a`.
The ISRG Root X1 trust anchor (Let's Encrypt, used by httpbin.org) is
pre-generated in `trust_anchors.h`.
No extra downloads or Python dependencies needed for the default target.

---

## Configuration

### WiFi — `wifi_config.h`

```c
#define WIFI_SSID     "YourNetwork"
#define WIFI_PASSWORD "YourPassword"
```

### Endpoint — `http_config.h`

```c
#define HTTP_HOST  "httpbin.org"   /* server hostname */
#define HTTP_PORT  443             /* HTTPS port      */
#define HTTP_PATH  "/ip"           /* request path    */

#define HTTP_RESP_BUF  2048        /* response capture size in bytes */
```

### Buffer sizes — `bearssl_conn.h`

```c
#define BEAR_IOBUF_SIZE  6144   /* BearSSL TLS record I/O buffer (half-duplex) */
#define BEAR_RXBUF_SIZE  4096   /* TCP receive ring buffer                      */
```

`BEAR_IOBUF_SIZE` must be at least as large as the biggest TLS record the
server sends.  Most servers' Certificate messages fit in 6 KB; large chains
(multiple intermediates) may need 8192 or 16384.  Increase if the handshake
fails with a BearSSL error code.

### Certificate date — `bearssl_conn.c`

BearSSL validates certificate dates against a fixed time you supply:

```c
/* 2026-05-01 00:00 UTC → BearSSL days = unix_ts/86400 + 719528 = 740102 */
br_x509_minimal_set_time(&g_xc, 740102u, 0u);
```

The ESP8266 has no RTC.  This constant is set to a date inside the
certificate's validity window.  If the server rotates its certificate and the
new chain's `notBefore` is later than this date, update the constant:

```python
import datetime
epoch = datetime.date(2000, 1, 1)
days  = (datetime.date(YEAR, MONTH, DAY) - epoch).days + 730119
print(days)   # pass this value to br_x509_minimal_set_time
```

---

## Adding a CA certificate (custom server)

The pre-built `trust_anchors.h` contains only ISRG Root X1 (Let's Encrypt).
To connect to a different server, regenerate the header with the matching root CA.

### Step 1 — get the root CA in PEM format

**Let's Encrypt** (already in `lib/`):
```bash
# already at lib/isrg_root_x1.pem — no download needed
```

**Amazon (AWS IoT, API Gateway)**:
```bash
# already at lib/amazon_root_ca1.pem — no download needed
```

**Any other server** — find the root CA in your browser:
*Lock icon → Certificate → Certification Path → select root → Export as PEM.*

Or fetch it with OpenSSL:
```bash
openssl s_client -connect yourserver.com:443 -showcerts </dev/null 2>/dev/null \
    | awk '/-----BEGIN/,/-----END/' | tail -n +$((...))
```

### Step 2 — generate `trust_anchors.h`

```bash
pip install cryptography          # one-time

# Single root:
python3 make_trust_anchors.py lib/isrg_root_x1.pem > trust_anchors.h

# Multiple roots (e.g. Let's Encrypt + Amazon):
python3 make_trust_anchors.py lib/isrg_root_x1.pem lib/amazon_root_ca1.pem > trust_anchors.h

# Self-signed server CA:
python3 make_trust_anchors.py my_ca.pem > trust_anchors.h
```

`make_trust_anchors.py` supports both RSA and EC (P-256, P-384, P-521) root keys.

### Step 3 — rebuild

```bash
make
```

---

## Build

```bash
make              # produces https_bear_ssl-0x00000.bin and https_bear_ssl-0x10000.bin
make clean        # remove build artefacts
```

BearSSL adds ~180–200 KB of code to the IROM image.  The 424 KB OTA partition
defined in `startup.c` accommodates this comfortably.

---

## Flash

### Routine update

```bash
make flash PORT=/dev/ttyUSB0
```

| Address    | Content                                    |
|------------|--------------------------------------------|
| `0x00000`  | `https_bear_ssl-0x00000.bin` (IRAM image)  |
| `0x10000`  | `https_bear_ssl-0x10000.bin` (IROM/XIP)    |
| `0x3FC000` | `esp_init_data_default_v08.bin` (PHY init) |
| `0x3FD000` | `blank.bin` (system parameters)            |

### First flash / after chip-erase

```bash
make flash_full PORT=/dev/ttyUSB0
```

Also writes `blank.bin` to `0x3FB000` (RF calibration).
**Do not use for routine updates** — it resets WiFi calibration data.

### WSL users

If the serial monitor holds the port, release it before flashing:

```powershell
# PowerShell (attach USB device to WSL):
usbipd list
usbipd attach --wsl --busid <x-y>
```

```bash
# WSL (grant port access):
sudo chmod a+rw /dev/ttyUSB0
make flash PORT=/dev/ttyUSB0
```

---

## Expected output

Serial monitor at **74880 8N1**:

```
================================
   KTOS BearSSL TLS 1.2 GET
================================
Host   : httpbin.org
Path   : /ip
Mode   : TLS 1.2 (BearSSL)
----
Status : HTTP/1.1 200 OK
Body   :
{
  "origin": "203.0.113.42"
}
----
Done. (256 bytes total)
```

### Error messages

| Output line                                | Cause                                                           |
|--------------------------------------------|-----------------------------------------------------------------|
| `DNS resolution failed`                    | `HTTP_HOST` unreachable or wrong SSID/password                  |
| `TLS 1.2 handshake or TCP connection failed` | Wrong trust anchor, cert date mismatch, or `BEAR_IOBUF_SIZE` too small |
| `BearSSL err=N` (non-zero N)               | See BearSSL `BR_ERR_*` constants in `lib/bearssl_inc/bearssl_ssl.h` |

---

## How it works

`startup.c` opens a plain TCP connection via `espconn_connect`.  In the TCP
connect callback, `bearssl_init()` initialises the BearSSL engine and starts
the TLS handshake.  From that point, two espconn callbacks drive everything:

- **`bearssl_on_recv`** — feeds incoming TCP bytes into a ring buffer, then
  runs `bear_drive`.
- **`bearssl_on_sent`** — releases BearSSL's outgoing record buffer, then
  runs `bear_drive`.

`bear_drive` (`bearssl_conn.c`) loops over BearSSL engine states in priority
order until it blocks:

```
BR_SSL_SENDREC  → espconn_sent(encrypted record), return (resume in sentcb)
BR_SSL_SENDAPP  → inject HTTP GET, flush              ← checked before RECVREC
BR_SSL_RECVREC  → feed ring buffer → engine, or return (wait for recvcb)
BR_SSL_RECVAPP  → copy plaintext to g_resp_buf
BR_SSL_CLOSED   → do_exit() → callx0 → KTOS
```

`SENDAPP` is checked before `RECVREC` because BearSSL sets both flags
simultaneously at handshake completion.  Checking `RECVREC` first with an
empty ring buffer would return early and the HTTP GET would never be injected.

`ets_run()` drives the WiFi stack and espconn callbacks throughout — nothing
blocks the event loop.

---

## RAM budget

Approximate heap usage during a TLS 1.2 handshake on KTOS:

| Component                              | Heap       |
|----------------------------------------|------------|
| ESP8266 WiFi stack (SDK, fixed)        | ~44 KB     |
| KTOS scheduler + task stack            | ~2–4 KB    |
| `br_ssl_client_context`                | ~1.8 KB    |
| `br_x509_minimal_context`              | ~3.3 KB    |
| BearSSL I/O buffer (`BEAR_IOBUF_SIZE`) | 6 KB       |
| TCP receive ring buffer (`BEAR_RXBUF_SIZE`) | 4 KB  |
| Response buffer (`HTTP_RESP_BUF`)      | 2 KB       |
| **Total used**                         | **~63 KB** |
| **Remaining from 80 KB**               | **~17 KB** |

The same BearSSL setup on a typical Arduino sketch leaves 0–3 KB — causing
`malloc` failures mid-handshake.

---

## TLS stack comparison

| Feature                  | axTLS (SDK built-in) | BearSSL                         |
|--------------------------|----------------------|---------------------------------|
| TLS versions             | 1.0, 1.1             | **1.2**                         |
| Key exchange             | RSA only             | RSA, **ECDHE-RSA**, ECDHE-ECDSA |
| Symmetric ciphers        | AES-CBC              | AES-GCM, **ChaCha20-Poly1305**  |
| Server compatibility     | ~2020 and earlier    | All modern servers              |
| Heap — Arduino           | ~14–18 KB (tight)    | ~14–18 KB (often fails)         |
| Heap — KTOS              | ~30–32 KB free       | **~17 KB free (works reliably)**|
