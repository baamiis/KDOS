#!/usr/bin/env python3
"""
fake_ktos_device.py — Simulates a KTOS MCU over a serial port.

Use this to test the ROS 2 serial bridge on a PC without real hardware.

Usage
-----
With a serial port (real or virtual):
    python3 fake_ktos_device.py --port /dev/pts/5 --baud 115200

Without a port (prints socat instructions):
    python3 fake_ktos_device.py

Creating a virtual serial pair with socat
-----------------------------------------
1. Install socat:
       sudo apt install socat

2. Create a virtual serial pair:
       socat -d -d pty,raw,echo=0 pty,raw,echo=0

   socat will print two device paths, e.g.:
       /dev/pts/3   ← connect the fake KTOS device here
       /dev/pts/4   ← connect the ROS 2 bridge here

3. Run this script on the first port:
       python3 tools/fake_ktos_device.py --port /dev/pts/3

4. Run the ROS 2 bridge on the second port:
       ros2 run ktos_serial_bridge serial_bridge_node \\
           --ros-args -p serial_port:=/dev/pts/4

Simulated behaviour
-------------------
Sends:
    HEARTBEAT,<n>                           every 1 s
    STATUS,<uptime_ms>,<led>,<mtr>,<sns>   every 2 s

Responds to:
    PING          -> PONG
    LED,ON        -> sets led_state = 1
    LED,OFF       -> sets led_state = 0
    MOTOR,<speed> -> sets motor_speed (clamped to -100..100)
    <unknown>     -> ERROR,UNKNOWN_CMD
"""

import argparse
import sys
import threading
import time

try:
    import serial
except ImportError:
    sys.exit("ERROR: pyserial not found. Install with: pip install pyserial")


# ---------------------------------------------------------------------------
# Simulated device state
# ---------------------------------------------------------------------------

class DeviceState:
    def __init__(self):
        self.led_state    = 0
        self.motor_speed  = 0
        self.sensor_value = 0
        self.heartbeat_n  = 0
        self.start_time   = time.monotonic()
        self._lock        = threading.Lock()

    def uptime_ms(self):
        return int((time.monotonic() - self.start_time) * 1000)

    def tick_sensor(self):
        """Oscillate sensor value between 0 and 1023."""
        with self._lock:
            self.sensor_value = int(512 + 511 * __import__('math').sin(
                time.monotonic() * 0.5
            ))

    def next_heartbeat(self):
        with self._lock:
            n = self.heartbeat_n
            self.heartbeat_n += 1
        return n

    def set_led(self, state: int):
        with self._lock:
            self.led_state = state

    def set_motor(self, speed: int):
        speed = max(-100, min(100, speed))
        with self._lock:
            self.motor_speed = speed

    def snapshot(self):
        with self._lock:
            return self.uptime_ms(), self.led_state, self.motor_speed, self.sensor_value


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def log_rx(line: str):
    print(f"  [RX] {line}", flush=True)

def log_tx(line: str):
    print(f"  [TX] {line}", flush=True)

def send(ser: serial.Serial, text: str):
    """Send a newline-terminated line and log it."""
    line = text.rstrip('\n') + '\n'
    ser.write(line.encode('ascii'))
    log_tx(text)


# ---------------------------------------------------------------------------
# Command parser
# ---------------------------------------------------------------------------

def handle_command(line: str, state: DeviceState, ser: serial.Serial):
    line = line.strip()
    log_rx(line)

    if line == 'PING':
        send(ser, 'PONG')
        return

    if line == 'LED,ON':
        state.set_led(1)
        print("  [STATE] LED = ON", flush=True)
        return

    if line == 'LED,OFF':
        state.set_led(0)
        print("  [STATE] LED = OFF", flush=True)
        return

    if line.startswith('MOTOR,'):
        try:
            speed = int(line[6:])
            state.set_motor(speed)
            print(f"  [STATE] MOTOR = {state.motor_speed}", flush=True)
        except ValueError:
            send(ser, 'ERROR,BAD_MOTOR_SPEED')
        return

    send(ser, 'ERROR,UNKNOWN_CMD')


# ---------------------------------------------------------------------------
# Background threads
# ---------------------------------------------------------------------------

def heartbeat_thread(state: DeviceState, ser: serial.Serial, stop: threading.Event):
    """Send HEARTBEAT every 1 second."""
    while not stop.is_set():
        n = state.next_heartbeat()
        try:
            send(ser, f'HEARTBEAT,{n}')
        except serial.SerialException:
            break
        stop.wait(1.0)


def status_thread(state: DeviceState, ser: serial.Serial, stop: threading.Event):
    """Send STATUS every 2 seconds."""
    stop.wait(2.0)   # stagger so first status doesn't overlap first heartbeat
    while not stop.is_set():
        state.tick_sensor()
        uptime, led, mtr, sns = state.snapshot()
        try:
            send(ser, f'STATUS,{uptime},{led},{mtr},{sns}')
        except serial.SerialException:
            break
        stop.wait(2.0)


def reader_thread(state: DeviceState, ser: serial.Serial, stop: threading.Event):
    """Read and dispatch incoming command lines."""
    buf = b''
    while not stop.is_set():
        try:
            chunk = ser.read(64)
        except serial.SerialException:
            break
        if not chunk:
            continue
        buf += chunk
        while b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            text = line.decode('ascii', errors='replace').strip()
            if text:
                handle_command(text, state, ser)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def print_socat_instructions():
    print("""
No serial port specified.

To create a virtual serial pair on Linux:

  sudo apt install socat          # if not already installed

  socat -d -d pty,raw,echo=0 pty,raw,echo=0

socat will print two device paths, for example:

  /dev/pts/3   ← use this for fake_ktos_device.py
  /dev/pts/4   ← use this for the ROS 2 bridge

Then run:

  Terminal 2:
    python3 tools/fake_ktos_device.py --port /dev/pts/3

  Terminal 3:
    ros2 run ktos_serial_bridge serial_bridge_node \\
        --ros-args -p serial_port:=/dev/pts/4
""")


def main():
    parser = argparse.ArgumentParser(
        description='Fake KTOS device — simulates MCU firmware over serial.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument('--port', '-p', default=None,
                        help='Serial port (e.g. /dev/pts/3 or /dev/ttyUSB0)')
    parser.add_argument('--baud', '-b', type=int, default=115200,
                        help='Baud rate (default: 115200)')
    args = parser.parse_args()

    if args.port is None:
        print_socat_instructions()
        sys.exit(0)

    print(f"[fake_ktos] Opening {args.port} at {args.baud} baud …")
    try:
        ser = serial.Serial(args.port, baudrate=args.baud, timeout=0.1)
    except serial.SerialException as e:
        sys.exit(f"ERROR: Cannot open {args.port}: {e}")

    print(f"[fake_ktos] Simulating KTOS device. Press Ctrl+C to stop.\n")

    state = DeviceState()
    stop  = threading.Event()

    threads = [
        threading.Thread(target=heartbeat_thread, args=(state, ser, stop), daemon=True),
        threading.Thread(target=status_thread,    args=(state, ser, stop), daemon=True),
        threading.Thread(target=reader_thread,    args=(state, ser, stop), daemon=True),
    ]
    for t in threads:
        t.start()

    try:
        while True:
            time.sleep(0.1)
    except KeyboardInterrupt:
        print("\n[fake_ktos] Stopping.")
    finally:
        stop.set()
        ser.close()


if __name__ == '__main__':
    main()
