# KTOS + ROS 2 Serial Bridge Demo

This example shows a **ROS 2 computer** communicating with a **microcontroller
running KTOS** over USB serial / UART.

KTOS is NOT replacing ROS 2.  KTOS runs on the small MCU side as the
cooperative task scheduler.  ROS 2 runs on Linux / Raspberry Pi / Jetson.
The bridge connects both sides using a simple newline-delimited ASCII protocol.

---

## Architecture

```
  ┌─────────────────────────────────────────────────┐
  │        ROS 2 Computer / Raspberry Pi / Jetson   │
  │                                                 │
  │   /ktos/cmd  ──► ktos_serial_bridge_node        │
  │                         │                       │
  │   /ktos/heartbeat ◄─────┤                       │
  │   /ktos/status    ◄─────┘                       │
  └──────────────────────┬──────────────────────────┘
                         │ USB Serial / UART (115200 8N1)
  ┌──────────────────────┴──────────────────────────┐
  │                 MCU running KTOS                │
  │                                                 │
  │   ┌─────────────┐   ┌─────────────┐            │
  │   │  Heartbeat  │   │   Sensor    │            │
  │   │  task (1 s) │   │ task (0.5s) │            │
  │   └─────────────┘   └─────────────┘            │
  │   ┌─────────────┐   ┌─────────────┐            │
  │   │   Status    │   │  Serial RX  │            │
  │   │  task (2 s) │   │ task (20ms) │            │
  │   └─────────────┘   └──────┬──────┘            │
  │   ┌─────────────┐          │                   │
  │   │   Command   │◄─────────┘                   │
  │   │    task     │                               │
  │   └──────┬──────┘                               │
  │          │                                      │
  │   ┌──────┴──────────────────┐                  │
  │   │       Board HAL         │                  │
  │   │  LED · Motor · UART     │                  │
  │   └─────────────────────────┘                  │
  └─────────────────────────────────────────────────┘
```

---

## Serial protocol summary

| Direction       | Message                           | Meaning              |
|-----------------|-----------------------------------|----------------------|
| ROS 2 → MCU     | `LED,ON\n`                        | Turn LED on          |
| ROS 2 → MCU     | `LED,OFF\n`                       | Turn LED off         |
| ROS 2 → MCU     | `MOTOR,<speed>\n`                 | Set motor (−100…100) |
| ROS 2 → MCU     | `PING\n`                          | Ping                 |
| MCU → ROS 2     | `HEARTBEAT,<n>\n`                 | Every 1 s            |
| MCU → ROS 2     | `STATUS,<ms>,<led>,<mtr>,<sns>\n` | Every 2 s            |
| MCU → ROS 2     | `PONG\n`                          | Reply to PING        |
| MCU → ROS 2     | `ERROR,<msg>\n`                   | Parse / apply error  |

Full protocol reference: [`protocol/ktos_serial_protocol.md`](protocol/ktos_serial_protocol.md)

---

## ROS 2 package — build and run

### Prerequisites

```bash
# Ubuntu 22.04 with ROS 2 Humble (or Iron / Jazzy)
sudo apt install python3-colcon-common-extensions
pip install pyserial
```

### Build

```bash
cd examples/ros2_serial_bridge/ros2_ws
colcon build
source install/setup.bash
```

### Run

```bash
ros2 run ktos_serial_bridge serial_bridge_node \
    --ros-args -p serial_port:=/dev/ttyUSB0 -p baud_rate:=115200
```

The node will attempt to reconnect automatically if the device is unplugged.

---

## Sending commands

```bash
# Turn LED on
ros2 topic pub /ktos/cmd std_msgs/msg/String "{data: 'LED,ON'}" --once

# Turn LED off
ros2 topic pub /ktos/cmd std_msgs/msg/String "{data: 'LED,OFF'}" --once

# Set motor to 50 % forward
ros2 topic pub /ktos/cmd std_msgs/msg/String "{data: 'MOTOR,50'}" --once

# Stop motor
ros2 topic pub /ktos/cmd std_msgs/msg/String "{data: 'MOTOR,0'}" --once

# Ping round-trip
ros2 topic pub /ktos/cmd std_msgs/msg/String "{data: 'PING'}" --once
```

## Listening to telemetry

```bash
# Heartbeat (1 Hz)
ros2 topic echo /ktos/heartbeat

# Full status (0.5 Hz)
ros2 topic echo /ktos/status
```

---

## Expected output

**ROS 2 terminal:**
```
[INFO] [ktos_serial_bridge_node]: Connected to /dev/ttyUSB0 at 115200 baud
[INFO] [ktos_serial_bridge_node]: [HEARTBEAT] HEARTBEAT,0
[INFO] [ktos_serial_bridge_node]: [HEARTBEAT] HEARTBEAT,1
[INFO] [ktos_serial_bridge_node]: [STATUS]    STATUS,2012,0,0,26
[INFO] [ktos_serial_bridge_node]: TX: LED,ON
[INFO] [ktos_serial_bridge_node]: [HEARTBEAT] HEARTBEAT,2
[INFO] [ktos_serial_bridge_node]: [STATUS]    STATUS,4018,1,0,52
```

**KTOS firmware terminal (115200 8N1):**
```
[HAL] board_init() — POSIX simulation
[KTOS] Tasks registered — starting scheduler
HEARTBEAT,0
HEARTBEAT,1
STATUS,2012,0,0,26
[HAL] LED ON
HEARTBEAT,2
STATUS,4018,1,0,52
```

---

## Porting the firmware to a real board

1. Edit `ktos_firmware/board_hal.c` — implement each `TODO` section.
2. Wire `ktos_timer_irq_handler()` to the 1 ms hardware tick ISR.
3. Wire `board_serial_read_char()` to a non-blocking UART RX buffer.
4. Wire `board_serial_write()` to a non-blocking UART TX (DMA or ring buffer).
5. Compile with your MCU toolchain alongside `core/ktos.c` and the BSP.

See `ktos_firmware/README.md` for board-specific snippets (STM32, AVR, ESP32).

---

## Why KTOS with ROS 2?

| Challenge                          | KTOS advantage                                     |
|------------------------------------|----------------------------------------------------|
| MCU RAM too small for an RTOS      | KTOS fits in ~4 KB flash, <1 KB RAM overhead       |
| Unpredictable task timing          | Cooperative scheduling — no preemption surprises   |
| Complex IPC between tasks          | Simple message queue, no semaphores needed         |
| Hard to debug bare-metal ISR code  | Tasks are plain C functions, easy to step through  |
| Port to different MCU families     | Swap only `board_hal.c` and the BSP assembly stub  |

---

## Testing without hardware

You can run a complete end-to-end demo on a single Linux PC using a virtual
serial pair.  No MCU required.

### Step 1 — create a virtual serial pair

**Terminal 1:**
```bash
sudo apt install socat    # one-time
socat -d -d pty,raw,echo=0 pty,raw,echo=0
```

socat prints two device paths — note them:
```
2025/xx/xx ... PTY is /dev/pts/3    ← X  (fake KTOS device uses this)
2025/xx/xx ... PTY is /dev/pts/4    ← Y  (ROS 2 bridge uses this)
```
Leave this terminal running.

### Step 2 — start the fake KTOS device

**Terminal 2:**
```bash
python3 tools/fake_ktos_device.py --port /dev/pts/3
```

You will see simulated HEARTBEAT and STATUS lines scrolling.  
`/dev/pts/3` is the device end — the fake MCU writes here.

### Step 3 — start the ROS 2 bridge

**Terminal 3:**
```bash
cd examples/ros2_serial_bridge/ros2_ws
colcon build && source install/setup.bash
ros2 run ktos_serial_bridge serial_bridge_node \
    --ros-args -p serial_port:=/dev/pts/4
```

`/dev/pts/4` is the host end — ROS 2 reads/writes here.

### Step 4 — send commands and read telemetry

**Terminal 4:**
```bash
# Listen to telemetry
ros2 topic echo /ktos/heartbeat
ros2 topic echo /ktos/status

# Send LED commands
ros2 topic pub /ktos/cmd std_msgs/msg/String "{data: 'LED,ON'}"  --once
ros2 topic pub /ktos/cmd std_msgs/msg/String "{data: 'LED,OFF'}" --once

# Set motor speed
ros2 topic pub /ktos/cmd std_msgs/msg/String "{data: 'MOTOR,50'}" --once

# Ping round-trip
ros2 topic pub /ktos/cmd std_msgs/msg/String "{data: 'PING'}" --once
```

> **Tip:** run `fake_ktos_device.py --help` to see all options.  
> Run it with no arguments to get socat setup instructions printed automatically.

---

## Next steps

- [ ] Add a binary framing layer (COBS) for higher-throughput sensor streams
- [ ] Add a `/ktos/ping` service on the ROS 2 side for latency measurement
- [ ] Implement real `board_hal.c` for STM32F103 (Blue Pill)
- [ ] Add IMU / encoder topics to the STATUS message
- [ ] Add micro-ROS as an alternative transport (replace serial bridge entirely)
- [ ] CI: loopback test using `socat` virtual serial pair
