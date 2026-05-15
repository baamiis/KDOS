"""
KTOS Serial Bridge Node
=======================
Bridges a KTOS-based MCU (connected over USB serial / UART) to ROS 2.

Published topics:
  /ktos/heartbeat  (std_msgs/String)  — KTOS heartbeat messages
  /ktos/status     (std_msgs/String)  — KTOS status messages

Subscribed topics:
  /ktos/cmd        (std_msgs/String)  — commands forwarded to MCU over serial

Parameters:
  serial_port  (string, default: /dev/ttyUSB0)
  baud_rate    (int,    default: 115200)

Usage:
  ros2 run ktos_serial_bridge serial_bridge_node \\
      --ros-args -p serial_port:=/dev/ttyUSB0 -p baud_rate:=115200
"""

import threading
import time

import rclpy
from rclpy.node import Node
from std_msgs.msg import String

try:
    import serial
    import serial.serialutil
except ImportError:
    raise SystemExit(
        "pyserial not found. Install with: pip install pyserial"
    )

RECONNECT_DELAY = 3.0   # seconds between reconnect attempts


class KtosSerialBridgeNode(Node):

    def __init__(self):
        super().__init__('ktos_serial_bridge_node')

        # --- Parameters ---
        self.declare_parameter('serial_port', '/dev/ttyUSB0')
        self.declare_parameter('baud_rate', 115200)

        self._port = self.get_parameter('serial_port').get_parameter_value().string_value
        self._baud = self.get_parameter('baud_rate').get_parameter_value().integer_value

        # --- Publishers ---
        self._pub_heartbeat = self.create_publisher(String, '/ktos/heartbeat', 10)
        self._pub_status    = self.create_publisher(String, '/ktos/status',    10)

        # --- Subscriber ---
        self._sub_cmd = self.create_subscription(
            String, '/ktos/cmd', self._on_cmd, 10
        )

        # --- Serial state ---
        self._serial = None
        self._serial_lock = threading.Lock()

        # --- Start serial reader thread ---
        self._running = True
        self._reader_thread = threading.Thread(target=self._reader_loop, daemon=True)
        self._reader_thread.start()

        self.get_logger().info(
            f'KTOS serial bridge started — port={self._port} baud={self._baud}'
        )

    # ------------------------------------------------------------------
    # Serial connection management
    # ------------------------------------------------------------------

    def _connect(self):
        """Open the serial port. Returns True on success."""
        try:
            ser = serial.Serial(
                port=self._port,
                baudrate=self._baud,
                timeout=1.0,
            )
            with self._serial_lock:
                self._serial = ser
            self.get_logger().info(f'Connected to {self._port} at {self._baud} baud')
            return True
        except serial.serialutil.SerialException as e:
            self.get_logger().warn(f'Cannot open {self._port}: {e}')
            return False

    def _disconnect(self):
        """Close the serial port quietly."""
        with self._serial_lock:
            if self._serial and self._serial.is_open:
                try:
                    self._serial.close()
                except Exception:
                    pass
            self._serial = None

    # ------------------------------------------------------------------
    # Reader loop (runs in background thread)
    # ------------------------------------------------------------------

    def _reader_loop(self):
        """
        Continuously reads newline-terminated lines from the serial port
        and dispatches them to the appropriate ROS 2 publisher.
        Reconnects automatically if the device is unplugged.
        """
        while self._running:
            if not self._connect():
                time.sleep(RECONNECT_DELAY)
                continue

            try:
                while self._running:
                    with self._serial_lock:
                        ser = self._serial
                    if ser is None or not ser.is_open:
                        break

                    line = ser.readline()
                    if not line:
                        continue

                    text = line.decode('ascii', errors='replace').strip()
                    if text:
                        self._dispatch(text)

            except serial.serialutil.SerialException as e:
                self.get_logger().warn(f'Serial error: {e} — reconnecting in {RECONNECT_DELAY}s')
            except Exception as e:
                self.get_logger().error(f'Unexpected reader error: {e}')
            finally:
                self._disconnect()
                if self._running:
                    time.sleep(RECONNECT_DELAY)

    # ------------------------------------------------------------------
    # Message dispatcher
    # ------------------------------------------------------------------

    def _dispatch(self, line: str):
        """Route an incoming serial line to the correct ROS 2 topic."""
        self.get_logger().debug(f'RX: {line}')

        if line.startswith('HEARTBEAT'):
            msg = String()
            msg.data = line
            self._pub_heartbeat.publish(msg)
            self.get_logger().info(f'[HEARTBEAT] {line}')

        elif line.startswith('STATUS'):
            msg = String()
            msg.data = line
            self._pub_status.publish(msg)
            self.get_logger().info(f'[STATUS]    {line}')

        elif line == 'PONG':
            self.get_logger().info('[PONG] round-trip OK')

        elif line.startswith('ERROR'):
            self.get_logger().warn(f'[KTOS ERROR] {line}')

        else:
            self.get_logger().debug(f'[UNKNOWN]   {line}')

    # ------------------------------------------------------------------
    # Command subscriber callback
    # ------------------------------------------------------------------

    def _on_cmd(self, msg: String):
        """
        Forward a /ktos/cmd message to the MCU over serial.
        Appends \\n if the caller omitted it.
        """
        cmd = msg.data.strip()
        if not cmd:
            return

        line = cmd + '\n'

        with self._serial_lock:
            ser = self._serial

        if ser is None or not ser.is_open:
            self.get_logger().warn(f'Serial not connected — dropping command: {cmd}')
            return

        try:
            ser.write(line.encode('ascii'))
            self.get_logger().info(f'TX: {cmd}')
        except serial.serialutil.SerialException as e:
            self.get_logger().error(f'Failed to send command: {e}')

    # ------------------------------------------------------------------
    # Cleanup
    # ------------------------------------------------------------------

    def destroy_node(self):
        self._running = False
        self._disconnect()
        super().destroy_node()


def main(args=None):
    rclpy.init(args=args)
    node = KtosSerialBridgeNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
