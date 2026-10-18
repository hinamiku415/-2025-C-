#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
video_pipeline_runner.py — 树莓派真实视觉测量系统 (端到端视频流处理与可视化)
使用真正的 yolov8n_phase2_int8.tflite (LiteRT 加速) + core.detect_a4 + modes.trigger
生成包含左侧原图检测与右侧透视展开/尺寸数字标注的高清双屏实测视频。
"""

import os
import sys
import time
import argparse
import cv2
import numpy as np

# 导入真实工程模块
from config import *
from modes import Modes
import core
import viz
from yolo_tflite import YOLOTFLite

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_IN_VID = os.path.join(BASE_DIR, "stress_tilt_video.mp4")
DEFAULT_OUT_VID = os.path.join(BASE_DIR, "pi_real_output.mp4")

def run_video_pipeline(input_video_path, output_video_path, mode=4):
    print(f"\n==================================================")
    print(f"🎬 启动真实视觉测量系统视频流端到端处理...")
    print(f"  输入源: {input_video_path}")
    print(f"  输出保存至: {output_video_path}")
    print(f"  测试模式: 模式 {mode} (多目标重叠方块与手写数字测量)")
    print(f"==================================================")

    detector = YOLOTFLite(YOLO_MODEL_PATH, num_threads=4)
    modes_processor = Modes()

    cap = cv2.VideoCapture(input_video_path)
    if not cap.isOpened():
        raise FileNotFoundError(f"无法打开输入视频: {input_video_path}")

    total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    fps_in = cap.get(cv2.CAP_PROP_FPS) or 30.0

    # 双屏并排输出分辨率 (1280 x 720)
    out_w, out_h = 1280, 720
    fourcc = cv2.VideoWriter_fourcc(*'mp4v')
    writer = cv2.VideoWriter(output_video_path, fourcc, fps_in, (out_w, out_h))

    frame_idx = 0
    latencies = []
    start_time = time.perf_counter()

    while cap.isOpened():
        ret, frame = cap.read()
        if not ret:
            break

        frame_idx += 1
        t0 = time.perf_counter()

        # 1. 运行核心 A4 轮廓检测
        pts, method, _ = core.detect_a4(frame)
        distance_cm = None
        if pts is not None:
            distance_cm, _ = core.calc_distance_from_pts(pts)

        # 2. 生成左面板 (原图 + A4 追踪框 + 测距)
        left = viz.draw_left_panel(frame, pts, mode, distance_cm)

        # 3. 生成右面板 (透视展开 + 模式4 数字与边长测量)
        info = None
        if pts is not None:
            right, info = modes_processor.trigger(frame, pts, mode)
            if mode in (3, 4, 5) and info and 'squares' in info:
                px_per_cm = core.get_px_per_cm()
                detections = info.get('detections')
                right = viz.draw_squares(right, info.get('squares', []), px_per_cm, detections)
        else:
            right = np.zeros((WARP_H, WARP_W, 3), dtype=np.uint8)
            viz.put_text(right, "Searching Target...", (40, 100), color=(0, 255, 255))

        # 4. 拼接左右双屏面板
        right_resized = cv2.resize(right, (left.shape[1], left.shape[0]))
        view = np.hstack((left, right_resized))
        dual_screen_resized = cv2.resize(view, (out_w, out_h))

        infer_time = (time.perf_counter() - t0) * 1000
        latencies.append(infer_time)
        fps_curr = 1000.0 / infer_time if infer_time > 0 else 30.0

        writer.write(dual_screen_resized)

        if frame_idx % 15 == 0 or frame_idx == total_frames:
            dist_str = f"{distance_cm:.1f}cm" if distance_cm else "N/A"
            squares = info.get('squares', []) if info else []
            sq_summary = ", ".join([f"[{s.get('number')}:{s.get('side', 0):.1f}cm]" for s in squares]) if squares else "None"
            print(f"  [Frame {frame_idx:03d}/{total_frames}] 耗时: {infer_time:.1f}ms ({fps_curr:.1f} FPS) | 距离: {dist_str} | 识别: {sq_summary}")

    cap.release()
    writer.release()
    total_sec = time.perf_counter() - start_time

    print(f"\n🎉 真实视觉测量视频处理圆满完成！")
    print(f"  平均单帧耗时: {np.mean(latencies):.2f} ms")
    print(f"  全流程处理吞吐: {len(latencies)/total_sec:.1f} FPS")
    print(f"  高清结果视频已保存至: {output_video_path}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--video", type=str, default=DEFAULT_IN_VID)
    parser.add_argument("--output", type=str, default=DEFAULT_OUT_VID)
    parser.add_argument("--mode", type=int, default=4)
    args = parser.parse_args()

    run_video_pipeline(args.video, args.output, args.mode)
