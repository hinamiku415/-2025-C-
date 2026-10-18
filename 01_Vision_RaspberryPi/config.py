#!/usr/bin/env python3
"""
config.py — 全局配置
====================
所有可调参数集中在此文件，方便修改。
"""

# ===== 串口配置 (STM32 发射台通信) =====
SERIAL_PORT = '/dev/ttyUSB0'    # USB转TTL，Pi上通常是 ttyUSB0
SERIAL_BAUD = 115200
USE_CHECKSUM = True             # 是否启用校验和防干扰 (格式: $DATA*CS\r\n)

# ===== 蓝牙配置 (JDY-31，树莓派直控目标小车) =====
ENABLE_BT = True                         # True=启用蓝牙控制小车，False=关闭
BT_MAC_ADDRESS = "5C:F1:50:14:D6:02"   # JDY-31 蓝牙从机 MAC 地址
BT_CHANNEL = 1                           # RFCOMM 经典蓝牙通道

# ===== 相机与分辨率 =====
RESOLUTION = (1280, 720)
FOCAL_LENGTH = 1697.80          # 焦距（像素），需根据实际相机标定

# ===== 目标物黑框物理尺寸（实测值）=====
A4_W_CM = 21.0                  # 黑框宽度 cm
A4_H_CM = 29.7                  # 黑框高度 cm

# ===== 白色内容区域尺寸 =====
INNER_W_CM = 19.2               # 白色区域宽度 cm
INNER_H_CM = 27.5               # 白色区域高度 cm

# ===== 边长校正系数 =====
# 测量值 × SIDE_CORRECTION = 真实值
SIDE_CORRECTION = 1.052

# ===== 透视变换输出尺寸 =====
PX_PER_CM = 30
WARP_W = int(A4_W_CM * PX_PER_CM)   # 630
WARP_H = int(A4_H_CM * PX_PER_CM)   # 891

# ===== 外框检测参数 =====
FRAME_SMOOTH_WINDOW = 8         # 时间平滑窗口帧数
FRAME_LOST_THRESHOLD = 50       # 连续丢失阈值（帧）
ASPECT_RATIO_TOL = 0.20         # A4宽高比容差

# ===== 内部图形检测参数 =====
MIN_SIDE_PX = 80                # 正方形最小边长 px
SQUARE_SIDE_RATIO_MAX = 1.20    # 正方形边长比容差
SQUARE_ANGLE_TOL = 22           # 角度容差（度）

# ===== 多帧测量 =====
MEASURE_FRAMES = 5
MEASURE_INTERVAL_MS = 50        # 帧间间隔 ms

# ===== YOLO TFLite 配置 =====
import os

_BASE_DIR = os.path.dirname(os.path.abspath(__file__))
YOLO_MODEL_PATH = os.path.join(_BASE_DIR, 'deploy_rpi', 'yolov8n_phase2_int8.tflite')
YOLO_CONF_THRES = 0.25
YOLO_IOU_THRES = 0.45
YOLO_NUM_THREADS = 4            # Pi 4B 四核
YOLO_SKIP_FULLFRAME = True      # 模式4/5在未检测到正方形时跳过全图推理（提速10倍）

# ===== 倾斜矫正参数 =====
TILT_MIN_ANGLE = 30
TILT_MAX_ANGLE = 60
TILT_ANGLE_ERROR_TOL = 5

# ===== 显示 =====
FULLSCREEN = True               # 是否全屏显示
WINDOW_NAME = "Pi Vision"
