#!/usr/bin/env python3
"""
serial_comm.py — 串口通信模块
==============================
树莓派与STM32通信，只保留文本协议（简化版）。
从比赛代码 /root/yolo/num/比赛代码/pro/serial_comm.py 移植。
"""

import time
import threading

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    serial = None


def calc_checksum(text):
    """计算 XOR 校验和 (2位十六进制大写)"""
    cs = 0
    for ch in text.encode('utf-8'):
        cs ^= ch
    return f"{cs:02X}"


class SerialComm:
    """串口通信类（文本协议，支持 CRC/XOR 校验和防干扰）"""

    def __init__(self, port='/dev/ttyUSB0', baud=115200, timeout=0.1, use_checksum=False):
        self.port = port
        self.baud = baud
        self.timeout = timeout
        self.use_checksum = use_checksum
        self.ser = None
        self.is_open = False
        self._lock = threading.Lock()
        self._connect()

    def _connect(self):
        """连接串口"""
        if serial is None:
            print("[UART] pyserial not installed, serial disabled")
            self.is_open = False
            return
        try:
            self.ser = serial.Serial(
                port=self.port,
                baudrate=self.baud,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=self.timeout,
            )
            self.is_open = True
            print(f"[UART] Connected: {self.port} @ {self.baud} bps (checksum={self.use_checksum})")
        except Exception as e:
            print(f"[UART] Failed to open {self.port}: {e}")
            self.ser = None
            self.is_open = False

    def reconnect(self):
        """重新连接串口"""
        self.close()
        time.sleep(0.5)
        self._connect()

    def close(self):
        """关闭串口"""
        if self.ser and self.ser.is_open:
            self.ser.close()
            self.is_open = False
            print("[UART] Closed")

    @staticmethod
    def list_ports():
        """列出所有可用串口"""
        if serial is None:
            return []
        ports = serial.tools.list_ports.comports()
        return [(p.device, p.description) for p in ports]

    def send_text(self, text):
        """
        发送文本。
        若启用 use_checksum，格式封装为: $文本*CS\\r\\n
        否则为普通换行: 文本\\n
        """
        if not self.is_open or not self.ser:
            return False
        with self._lock:
            try:
                raw_text = text.strip()
                if self.use_checksum:
                    cs = calc_checksum(raw_text)
                    packet = f"${raw_text}*{cs}\r\n"
                else:
                    packet = f"{raw_text}\n"

                self.ser.write(packet.encode('utf-8'))
                self.ser.flush()
                print(f"[UART TX] {packet.strip()}")
                return True
            except Exception as e:
                print(f"[UART] Send error: {e}")
                return False

    def send_distance(self, distance_cm):
        """发送距离数据"""
        self.send_text(f"D:{distance_cm:.2f}")

    def send_shape(self, distance_cm, shape_type, size_cm):
        """发送形状数据（模式2）"""
        self.send_text(f"D:{distance_cm:.2f},shape:{shape_type},x:{size_cm:.2f}")

    def send_square(self, distance_cm, number, side_cm):
        """发送单个正方形数据"""
        self.send_text(f"D:{distance_cm:.2f},N:{number},x:{side_cm:.2f}")

    def send_multi_squares(self, distance_cm, squares):
        """发送多个正方形数据"""
        for sq in squares:
            self.send_square(distance_cm, sq.get('number', 0), sq.get('side', 0))
        self.send_status(mode=3, status_code=2)

    def send_status(self, mode, status_code=0):
        """发送状态信息"""
        self.send_text(f"STATUS:mode={mode},status={status_code}")

    def send_error(self, error_msg):
        """发送错误信息"""
        self.send_text(f"ERROR:{error_msg}")

    def readline(self):
        """
        读取一行文本（阻塞直到收到换行或超时）。
        自动兼容普通文本和带校验和包 ($DATA*CS)。
        若收到带校验包但校验失败，自动丢弃防误动作。
        """
        if not self.is_open or not self.ser:
            return ""
        try:
            line = self.ser.readline().decode('utf-8', errors='ignore').strip()
            if not line:
                return ""

            # 如果收到带校验格式 $...*CS
            if line.startswith('$') and '*' in line:
                content, recv_cs = line[1:].rsplit('*', 1)
                expected_cs = calc_checksum(content)
                if recv_cs.upper() == expected_cs:
                    print(f"[UART RX verified] {content}")
                    return content
                else:
                    print(f"[UART RX WARN] Checksum mismatch: expected {expected_cs}, got {recv_cs}")
                    return ""

            print(f"[UART RX] {line}")
            return line
        except Exception as e:
            print(f"[UART] Read error: {e}")
            return ""
