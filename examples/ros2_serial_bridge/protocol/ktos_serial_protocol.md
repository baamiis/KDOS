# KTOS Serial Bridge Protocol

Version: 1.0  
Encoding: ASCII, newline-terminated (`\n`)  
Baud rate: 115200 8N1  

---

## Overview

All messages are plain ASCII strings ending with `\n`.
Each message is a comma-separated line: `TYPE[,FIELD1[,FIELD2...]]\n`

This makes the protocol trivial to debug with any serial terminal and easy
to extend without a parser library on either side.

---

## ROS 2 → KTOS (commands)

### LED control

```
LED,ON\n
LED,OFF\n
```

Turns the onboard LED on or off.  
KTOS replies with `STATUS` on the next status cycle reflecting the new state.

---

### Motor speed

```
MOTOR,<speed>\n
```

`<speed>` is a signed integer in the range `[-100, 100]`.  
Positive = forward, negative = reverse, 0 = stop.

Examples:
```
MOTOR,50\n
MOTOR,0\n
MOTOR,-30\n
```

---

### Ping

```
PING\n
```

KTOS replies immediately with:
```
PONG\n
```

Useful for checking round-trip latency and connection health.

---

## KTOS → ROS 2 (telemetry)

### Heartbeat

Sent every **1000 ms**.

```
HEARTBEAT,<counter>\n
```

`<counter>` increments from 0 each power cycle.

Example:
```
HEARTBEAT,42\n
```

---

### Status

Sent every **2000 ms**.

```
STATUS,<uptime_ms>,<led_state>,<motor_speed>,<sensor_value>\n
```

| Field          | Type    | Description                         |
|----------------|---------|-------------------------------------|
| `uptime_ms`    | uint32  | Milliseconds since boot             |
| `led_state`    | 0 or 1  | 0 = off, 1 = on                     |
| `motor_speed`  | int     | Last commanded speed (−100 to 100)  |
| `sensor_value` | int     | Simulated sensor reading            |

Example:
```
STATUS,12345,1,50,712\n
```

---

### Pong

Reply to `PING`.

```
PONG\n
```

---

### Error

Sent when a command cannot be parsed or applied.

```
ERROR,<message>\n
```

Examples:
```
ERROR,UNKNOWN_CMD\n
ERROR,BAD_MOTOR_SPEED\n
```

---

## Design notes

- All messages are human-readable for easy debugging with `minicom` or `screen`.
- Newline (`\n`) is the sole message delimiter — no carriage return required.
- The ROS 2 node automatically appends `\n` if the caller omits it.
- Future versions may add a binary framing layer (e.g. COBS) for higher
  throughput, but the ASCII layer is sufficient for most robot demos.
