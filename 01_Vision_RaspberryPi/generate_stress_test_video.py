#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
generate_stress_test_video.py — 真实相机 3D 运动仿真器 (连续平滑拉远 + 3D 俯仰倾斜 + 偏航晃动)
直接对原始采集画面施加连续 3D 单应性透视变换，模拟无人机/小车逼近与远离物面的真实物理过程。
"""

import os
import cv2
import numpy as np

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_IMG = os.path.join(BASE_DIR, "tests", "test.jpg")
DEFAULT_OUT = os.path.join(BASE_DIR, "stress_tilt_video.mp4")

def generate_3d_camera_video(
    base_img_path=DEFAULT_IMG,
    output_video_path=DEFAULT_OUT,
    num_frames=120,
    fps=30
):
    img = cv2.imread(base_img_path)
    if img is None:
        raise FileNotFoundError(f"找不到基准测试图: {base_img_path}")

    h, w = img.shape[:2]
    fourcc = cv2.VideoWriter_fourcc(*'mp4v')
    writer = cv2.VideoWriter(output_video_path, fourcc, fps, (w, h))

    print(f"\n==================================================")
    print(f"🎬 正在进行真实相机 3D 连续远近与倾斜运动仿真...")
    print(f"  帧数: {num_frames} 帧 (4.0 秒) | 画布: {w}x{h}")
    print(f"==================================================")

    for i in range(num_frames):
        t = i / float(num_frames)

        # 1. 距离动态变化 (放远): 缩放因子从 1.0 (约35cm) 缩小到 0.65 (约58cm)，随后再平滑拉近
        scale = 1.0 - 0.35 * np.sin(t * np.pi)

        # 2. 3D 俯仰倾斜 (Pitch): 模拟相机从垂直俯视到 22° 倾斜
        pitch_deg = 22.0 * np.sin(t * np.pi)

        # 3. 偏航 (Yaw) 与位移 (Shake)
        yaw_deg = 8.0 * np.sin(t * 2 * np.pi)
        dx = int(np.sin(t * 2 * np.pi) * 35)
        dy = int(np.cos(t * np.pi) * 20)

        cx, cy = w / 2.0 + dx, h / 2.0 + dy
        rad_p = np.radians(pitch_deg)
        rad_y = np.radians(yaw_deg)

        # 原始四角
        src_pts = np.array([
            [0, 0],
            [w, 0],
            [w, h],
            [0, h]
        ], dtype=np.float32)

        # 3D 投影四角 (近大远小梯形)
        w_half = (w * scale) / 2.0
        h_half = (h * scale) / 2.0
        top_shrink = 1.0 - 0.18 * np.sin(rad_p)
        bot_expand = 1.0 + 0.18 * np.sin(rad_p)
        h_comp = np.cos(rad_p)

        dst_pts = np.array([
            [cx - w_half * top_shrink - np.sin(rad_y)*15, cy - h_half * h_comp],
            [cx + w_half * top_shrink - np.sin(rad_y)*15, cy - h_half * h_comp],
            [cx + w_half * bot_expand + np.sin(rad_y)*15, cy + h_half * h_comp],
            [cx - w_half * bot_expand + np.sin(rad_y)*15, cy + h_half * h_comp]
        ], dtype=np.float32)

        H = cv2.getPerspectiveTransform(src_pts, dst_pts)
        frame = cv2.warpPerspective(img, H, (w, h), borderMode=cv2.BORDER_REFLECT)

        writer.write(frame)

        if (i + 1) % 30 == 0:
            est_dist = 35.0 / scale
            print(f"  [Sim] 帧: {i+1:03d}/{num_frames} | 距离: ~{est_dist:.1f}cm | 倾斜角: {pitch_deg:.1f}°")

    writer.release()
    print(f"🎉 3D 仿真运动测试视频生成完毕: {output_video_path}\n")

if __name__ == "__main__":
    generate_3d_camera_video()
