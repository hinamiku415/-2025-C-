#!/usr/bin/env python3
"""
viz.py — 可视化层（Pi 全屏版）
================================
所有与显示、绘图相关的函数。
从 /root/yolo/num/test_photos/改造/viz.py 移植 + 全屏适配。
"""

import cv2
import numpy as np
from config import WARP_W, WARP_H, WINDOW_NAME, FULLSCREEN


def setup_window():
    """创建全屏窗口"""
    cv2.namedWindow(WINDOW_NAME, cv2.WINDOW_NORMAL)
    if FULLSCREEN:
        cv2.setWindowProperty(WINDOW_NAME, cv2.WND_PROP_FULLSCREEN,
                              cv2.WINDOW_FULLSCREEN)


def put_text(img, text, pos, scale=0.7, color=(255, 255, 255), thickness=2):
    """在图上写字（带黑色阴影）"""
    x, y = pos
    cv2.putText(img, text, (x, y), cv2.FONT_HERSHEY_SIMPLEX,
                scale, (0, 0, 0), thickness + 2)
    cv2.putText(img, text, (x, y), cv2.FONT_HERSHEY_SIMPLEX,
                scale, color, thickness)


def draw_left_panel(frame, outer_pts, mode, distance_cm=None):
    """
    画左面板：原图 + 绿色外框 + 距离/状态信息。

    返回
    -------
    left : ndarray
    """
    left = frame.copy()
    if mode == 0:
        put_text(left, "IDLE", (20, 40), color=(100, 100, 100))
    elif outer_pts is not None:
        cv2.polylines(left, [outer_pts.astype(np.int32)], True, (0, 255, 0), 3)
        if distance_cm is not None:
            put_text(left, f"D={distance_cm:.1f}cm", (20, 40), color=(0, 255, 0))
    else:
        put_text(left, "Searching A4...", (40, 60), color=(0, 0, 255))
    put_text(left, f"Mode {mode}", (20, left.shape[0] - 20),
             scale=0.5, color=(200, 200, 200))
    return left


def draw_right_idle():
    """画右面板：空闲模式"""
    right = np.zeros((WARP_H, WARP_W, 3), dtype=np.uint8)
    put_text(right, "IDLE MODE", (40, WARP_H // 2 - 20),
             scale=1.2, color=(80, 80, 80))
    put_text(right, "No data output", (40, WARP_H // 2 + 20),
             scale=0.8, color=(60, 60, 60))
    return right


def draw_right_no_a4():
    """画右面板：未检测到A4"""
    right = np.zeros((WARP_H, WARP_W, 3), dtype=np.uint8)
    put_text(right, "No A4 detected", (40, 60), color=(0, 0, 255))
    return right


def draw_right_waiting():
    """画右面板：等待触发"""
    right = np.zeros((WARP_H, WARP_W, 3), dtype=np.uint8)
    put_text(right, "Press TRIGGER to measure", (10, WARP_H // 2),
             scale=0.7, color=(0, 200, 255))
    return right


def draw_squares(frame, squares, px_per_cm, digits=None):
    """
    画检测到的正方形 + 数字标注。

    参数
    ----------
    frame : ndarray
    squares : list of dict (from core.detect_squares_in_roi)
    px_per_cm : float
    digits : list of (digit, conf, x1, y1, x2, y2) or None
    """
    vis = frame.copy()

    # 画正方形
    for i, sq in enumerate(squares):
        pts = sq['pts']
        side_cm = sq.get('side', sq.get('side_px', 0) / px_per_cm)
        cx = float(np.mean(pts[:, 0]))
        cy = float(np.mean(pts[:, 1]))

        # 配对数字
        digit_str = str(sq.get('number', '?'))
        if digit_str == '?' and digits:
            side_px = sq.get('side_px', side_cm * px_per_cm)
            best_dist = side_px * 0.6
            for cls_id, conf, x1, y1, x2, y2 in digits:
                ycx = (x1 + x2) / 2
                ycy = (y1 + y2) / 2
                d = np.linalg.norm(np.array([cx, cy]) - np.array([ycx, ycy]))
                if d < best_dist:
                    best_dist = d
                    digit_str = str(cls_id)

        color = (0, 255, 255)
        cv2.polylines(vis, [pts], True, color, 3)
        label = f"{digit_str}: {side_cm:.1f}cm"
        put_text(vis, label, (int(cx) - 40, int(cy) - 25), scale=0.7, color=color)

    return vis


def show_frame(frame):
    """显示一帧（自动拼接左右面板）"""
    cv2.imshow(WINDOW_NAME, frame)
