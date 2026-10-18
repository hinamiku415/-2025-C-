#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
蓝牙通信模块 - 树莓派通过 JDY-31 控制小车
使用原生蓝牙 RFCOMM Socket，无需 rfcomm bind
"""

import socket
import time
import threading

from config import BT_MAC_ADDRESS, BT_CHANNEL


class BTComm:
    """蓝牙通信类（经典蓝牙 SPP）"""

    def __init__(self):
        self.sock = None
        self.is_open = False
        self._lock = threading.Lock()

    def connect(self):
        """连接蓝牙模块"""
        try:
            print(f"[BT] Connecting to {BT_MAC_ADDRESS} ch={BT_CHANNEL}...")
            self.sock = socket.socket(
                socket.AF_BLUETOOTH,
                socket.SOCK_STREAM,
                socket.BTPROTO_RFCOMM
            )
            self.sock.connect((BT_MAC_ADDRESS, BT_CHANNEL))
            self.sock.settimeout(0.5)
            self.is_open = True
            print("[BT] Connected!")
            time.sleep(0.5)  # 等待模块稳定
        except Exception as e:
            print(f"[BT] Connect failed: {e}")
            self.sock = None
            self.is_open = False

    def close(self):
        """关闭连接"""
        if self.sock:
            try:
                self.sock.close()
            except:
                pass
            self.sock = None
            self.is_open = False
            print("[BT] Closed")

    def send(self, cmd: str):
        """发送指令（自动加 \\r\\n），断线自动重连"""
        if not self.is_open:
            self.connect()
        if not self.is_open or not self.sock:
            return False
        with self._lock:
            try:
                data = (cmd + "\r\n").encode('utf-8')
                self.sock.send(data)
                print(f"[BT TX] {cmd}")
                return True
            except Exception as e:
                print(f"[BT] Send error: {e}, reconnecting...")
                self.is_open = False
                self.connect()
                if self.is_open:
                    try:
                        data = (cmd + "\r\n").encode('utf-8')
                        self.sock.send(data)
                        print(f"[BT TX] {cmd} (after reconnect)")
                        return True
                    except:
                        pass
                return False

    def send_go(self):
        """发送启动指令"""
        return self.send("GO")

    def send_stop(self):
        """发送停车指令"""
        return self.send("STOP")

    def send_target(self, distance_cm: int):
        """发送目标距离（基础1用）"""
        return self.send(f"TARGET:{distance_cm}")

    def readline(self):
        """读取一行反馈（非阻塞，超时返回空字符串）"""
        if not self.is_open or not self.sock:
            return ""
        try:
            data = b""
            while True:
                byte = self.sock.recv(1)
                if not byte:
                    break
                if byte == b'\n':
                    break
                if byte != b'\r':
                    data += byte
            line = data.decode('utf-8', errors='ignore').strip()
            if line:
                print(f"[BT RX] {line}")
            return line
        except socket.timeout:
            return ""
        except Exception as e:
            # 不关闭连接，只打印错误
            print(f"[BT] Read error (ignored): {e}")
            return ""


# 全局实例
bt = BTComm()
