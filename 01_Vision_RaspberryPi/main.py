#!/usr/bin/env python3
"""
main.py — 树莓派实时视觉测量系统
===================================
仿照比赛代码架构，使用 TFLite INT8 推理。
摄像头采集 → A4检测 → 透视变换 → 模式处理 → 全屏显示 + 串口输出

用法：
  python main.py                        # 正常运行（摄像头 + 串口）
  python main.py --test-image 1.jpg     # 离线图片测试
  python main.py --no-serial            # 不连接串口
"""

import argparse
import cv2
import numpy as np
import time
import threading
from collections import deque

from config import *
from serial_comm import SerialComm
from modes import Modes
import core
import viz


# ════════════════════════════════════════════════════════════════
#  全局状态
# ════════════════════════════════════════════════════════════════
current_mode = 1
pending_mode = 1
target_n = 0
target_d = 0
measurement_done = False
_state_lock = threading.Lock()

# 帧平滑
outer_pts_buffer = deque(maxlen=FRAME_SMOOTH_WINDOW)
last_valid_outer = None
_lost_counter = 0
_lost_warned = False


def get_stable_outer(new_pts):
    """对外框检测结果进行时间平滑"""
    global last_valid_outer, _lost_counter, _lost_warned

    if new_pts is not None:
        outer_pts_buffer.append(new_pts)
        last_valid_outer = new_pts
        _lost_counter = 0
        _lost_warned = False
    else:
        _lost_counter += 1
        if _lost_counter >= FRAME_LOST_THRESHOLD and not _lost_warned:
            print(f"[WARN] 外框连续丢失 {_lost_counter} 帧")
            _lost_warned = True

    if len(outer_pts_buffer) >= 2:
        return np.mean(outer_pts_buffer, axis=0).astype(np.float32)
    return last_valid_outer


# ════════════════════════════════════════════════════════════════
#  摄像头初始化（后台守护抓帧，杜绝FIFO堆积延迟）
# ════════════════════════════════════════════════════════════════
picam2 = None
cap = None
_latest_frame = None
_frame_lock = threading.Lock()
_grabber_running = False
_grabber_thread = None


def _camera_grabber_worker():
    """守护线程：持续从硬件读取最新帧，排空底层 V4L2 缓冲区"""
    global _latest_frame, _grabber_running
    while _grabber_running:
        frame = None
        if picam2 is not None:
            try:
                arr = picam2.capture_array()
                frame = cv2.cvtColor(arr, cv2.COLOR_RGB2BGR)
            except Exception:
                pass
        elif cap is not None:
            ret, f = cap.read()
            if ret:
                frame = f

        if frame is not None:
            with _frame_lock:
                _latest_frame = frame
        else:
            time.sleep(0.005)


def init_camera():
    global picam2, cap, _grabber_running, _grabber_thread
    try:
        from picamera2 import Picamera2
        picam2 = Picamera2()
        picam2.configure(picam2.create_preview_configuration(
            main={"format": "RGB888", "size": RESOLUTION}
        ))
        picam2.start()
        print("[CAM] Picamera2 started")
    except Exception as e:
        print(f"[CAM] Picamera2 unavailable: {e}")
        picam2 = None

    if picam2 is None:
        cap = cv2.VideoCapture(0)
        if not cap.isOpened():
            cap = None
            print("[CAM] No camera found, using blank frames")
        else:
            cap.set(cv2.CAP_PROP_FRAME_WIDTH, RESOLUTION[0])
            cap.set(cv2.CAP_PROP_FRAME_HEIGHT, RESOLUTION[1])
            # 设置底层缓冲为1，双重降低硬件延时
            cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)

    if picam2 is not None or cap is not None:
        _grabber_running = True
        _grabber_thread = threading.Thread(target=_camera_grabber_worker, daemon=True)
        _grabber_thread.start()
        print("[CAM] Real-time grabber thread started (0-latency)")


def capture_frame():
    """获取物理世界的最新一帧 BGR 图像（0延迟）"""
    with _frame_lock:
        if _latest_frame is not None:
            return _latest_frame.copy()
    return np.zeros((RESOLUTION[1], RESOLUTION[0], 3), dtype=np.uint8)


def release_camera():
    """优雅释放摄像头资源"""
    global _grabber_running, picam2, cap
    _grabber_running = False
    time.sleep(0.05)
    if picam2 is not None:
        try:
            picam2.stop()
            picam2.close()
        except Exception:
            pass
    if cap is not None:
        try:
            cap.release()
        except Exception:
            pass


# ════════════════════════════════════════════════════════════════
#  发送结果到串口
# ════════════════════════════════════════════════════════════════

def send_mode_info(comm, info, mode):
    """根据模式发送结构化数据到串口"""
    if info is None:
        return
    d = info.get('distance', 0)

    if mode == 1:
        comm.send_distance(d)

    elif mode == 2:
        for s in info.get('shapes', []):
            t = s.get('type', '?')
            size = s.get('diameter', s.get('side', 0))
            comm.send_shape(d, t, size)

    elif mode == 3:
        min_side = info.get('min_side', 0)
        comm.send_text(f"D:{d:.2f},x:{min_side:.2f}")

    elif mode == 6:
        if 'error' in info:
            comm.send_error(info['error'])
        else:
            comm.send_text(f"CALIB:F:{info['focal_length']:.2f},D:{info['distance']:.2f}")

    else:
        # 模式 4/5
        if info.get('tilt_error'):
            comm.send_error("tilt estimation failed")
            return
        squares = info.get('squares', [])
        if target_n > 0:
            matched = [sq for sq in squares
                       if str(sq.get('number', '?')) == str(target_n)]
            if matched:
                sq = matched[0]
                comm.send_square(d, sq.get('number', '?'), sq.get('side', 0))
            else:
                comm.send_error(f"N={target_n} not found")
        else:
            if squares:
                comm.send_multi_squares(d, squares)
            else:
                comm.send_distance(d)


# ════════════════════════════════════════════════════════════════
#  后台帧处理线程
# ════════════════════════════════════════════════════════════════

def frame_worker(comm, modes, test_image=None, no_gui=False):
    """主循环：采集 → 检测 → 处理 → 显示"""
    global current_mode, pending_mode, target_n, target_d
    global measurement_done

    if not no_gui:
        viz.setup_window()

    while True:
        # 获取帧
        if test_image is not None:
            frame = cv2.imread(test_image)
            if frame is None:
                print(f"[ERR] Cannot read {test_image}")
                break
        else:
            frame = capture_frame()

        # A4 检测
        pts, method, _ = core.detect_a4(frame)
        with _state_lock:
            outer_pts = get_stable_outer(pts)
            mode = current_mode

        # 距离
        distance_cm = None
        if outer_pts is not None:
            distance_cm, _ = core.calc_distance_from_pts(outer_pts)

        # 左面板
        left = viz.draw_left_panel(frame, outer_pts, mode, distance_cm)

        # 右面板：模式处理
        info = None
        if mode == 0:
            right = viz.draw_right_idle()
        elif outer_pts is not None:
            try:
                if test_image is not None and mode >= 2:
                    right, info = modes.trigger(frame, outer_pts, mode)
                else:
                    right, info = modes.process(frame, outer_pts, mode)
                if info and mode == 1:
                    send_mode_info(comm, info, mode)
            except Exception as e:
                print(f"[ERR] Mode {mode}: {e}")
                right = np.zeros((WARP_H, WARP_W, 3), dtype=np.uint8)
                viz.put_text(right, f"Err: {str(e)[:40]}", (10, 40),
                             color=(0, 255, 255))
        else:
            right = viz.draw_right_no_a4()

        # 如果右面板有正方形检测结果，画出来
        if (mode in (3, 4, 5) and outer_pts is not None and
                'info' in dir() and info and 'squares' in info):
            px_per_cm = core.get_px_per_cm()
            detections = info.get('detections')
            right = viz.draw_squares(right, info.get('squares', []),
                                     px_per_cm, detections)

        # 拼接左右面板
        right_resized = cv2.resize(right, (left.shape[1], left.shape[0]))
        view = np.hstack((left, right_resized))

        # 显示
        if not no_gui:
            viz.show_frame(view)

            # 按键处理
            key = cv2.waitKey(1) & 0xFF
            if key == ord('q'):
                break
            elif key == ord('1'):
                with _state_lock:
                    current_mode = 1
                    modes.clear_frozen()
            elif key == ord('2'):
                with _state_lock:
                    current_mode = 2
                    modes.clear_frozen()
            elif key == ord('3'):
                with _state_lock:
                    current_mode = 3
                    modes.clear_frozen()
            elif key == ord('4'):
                with _state_lock:
                    current_mode = 4
                    modes.clear_frozen()
            elif key == ord('5'):
                with _state_lock:
                    current_mode = 5
                    modes.clear_frozen()
            elif key == ord('6'):
                with _state_lock:
                    current_mode = 6
                    modes.clear_frozen()
            elif key == ord('t'):
                # 触发测量
                if outer_pts is not None and mode >= 2:
                    roi, info = modes.trigger(frame, outer_pts, mode,
                                              capture_fn=capture_frame)
                    if info:
                        send_mode_info(comm, info, mode)
                        comm.send_status(mode, status_code=2)
                        print(f"[TRIGGER] Mode {mode} measurement done")

        # 离线测试只跑一帧
        if test_image is not None:
            print("\n=== 离线测试结果 ===")
            if outer_pts is not None:
                print(f"  A4 检测: {method}")
                print(f"  距离: {distance_cm:.1f} cm")
                if mode >= 2 and 'info' in dir() and info:
                    print(f"  模式 {mode} 结果:")
                    if 'squares' in info:
                        for sq in info['squares']:
                            print(f"    - 数字: {sq.get('number', '?')}, 边长: {sq.get('side', 0):.2f} cm")
                    else:
                        print(f"    {info}")
            else:
                print("  未检测到 A4 纸")
            
            # 保存离线测试结果图
            out_filename = "test_result.jpg"
            cv2.imwrite(out_filename, view)
            print(f"  结果图片已保存至: {out_filename}")

            if not no_gui:
                print("\n按任意键退出...")
                cv2.waitKey(0)
            break

    if not no_gui:
        cv2.destroyAllWindows()


# ════════════════════════════════════════════════════════════════
#  串口监听线程
# ════════════════════════════════════════════════════════════════

def uart_listener(comm, modes):
    """监听 STM32 命令，驱动模式切换"""
    global current_mode, pending_mode, target_n, target_d
    global measurement_done

    while True:
        line = comm.readline()
        if not line:
            time.sleep(0.01)
            continue

        if line == "TRIGGER":
            # 立即触发当前 pending_mode 测量
            with _state_lock:
                mode = pending_mode
            frame = capture_frame()
            pts, _, _ = core.detect_a4(frame)
            pts = get_stable_outer(pts)
            if pts is not None:
                roi, info = modes.trigger(frame, pts, mode,
                                          capture_fn=capture_frame)
                if info:
                    send_mode_info(comm, info, mode)
                    comm.send_status(mode, status_code=2)

        elif line.startswith("MODE:"):
            try:
                m = int(line.split(":")[1])
                if m in (0, 1):
                    with _state_lock:
                        current_mode = m
                        modes.clear_frozen()
                elif m in (2, 3, 4, 5, 6):
                    with _state_lock:
                        pending_mode = m
                        current_mode = 1  # 先切到测距模式
                        modes.clear_frozen()
                        measurement_done = False
            except ValueError:
                pass

        elif line.startswith("N="):
            try:
                target_n = int(line.split("=")[1])
            except ValueError:
                pass

        elif line.startswith("D="):
            try:
                target_d = float(line.split("=")[1])
            except ValueError:
                pass


# ════════════════════════════════════════════════════════════════
#  主入口
# ════════════════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(description="Pi 实时视觉测量系统")
    parser.add_argument("--test-image", help="离线图片测试模式")
    parser.add_argument("--mode", type=int, default=1, choices=[0, 1, 2, 3, 4, 5, 6], help="工作模式 (默认: 1)")
    parser.add_argument("--no-gui", action="store_true", help="无GUI模式 (不弹窗显示)")
    parser.add_argument("--no-serial", action="store_true", help="不连接串口")
    parser.add_argument("--serial-port", default=SERIAL_PORT, help="串口设备")
    args = parser.parse_args()

    global current_mode
    current_mode = args.mode

    # 初始化摄像头（离线测试不需要）
    if not args.test_image:
        init_camera()

    # 初始化串口
    comm = None
    if not args.no_serial and not args.test_image:
        try:
            comm = SerialComm(port=args.serial_port, baud=SERIAL_BAUD,
                              use_checksum=USE_CHECKSUM)
        except Exception as e:
            print(f"[UART] 串口初始化失败: {e}")
            comm = None

    if comm is None:
        # 创建一个空的 comm 对象（send_text 什么都不做）
        class _NullComm:
            def send_text(self, t): pass
            def send_distance(self, d): pass
            def send_shape(self, d, t, s): pass
            def send_square(self, d, n, s): pass
            def send_multi_squares(self, d, sq): pass
            def send_status(self, m, s=0): pass
            def send_error(self, e): pass
            def readline(self): return ""
        comm = _NullComm()

    # 初始化模式
    modes = Modes()

    print("=" * 50)
    print("Pi 实时视觉测量系统")
    print("=" * 50)
    print(f"  分辨率: {RESOLUTION}")
    print(f"  焦距: {FOCAL_LENGTH}")
    print(f"  模型: {YOLO_MODEL_PATH}")
    if args.test_image:
        print(f"  测试图片: {args.test_image}")
    print()
    print("按键操作:")
    print("  1-6 : 切换模式")
    print("  t   : 触发测量（模式2-6）")
    print("  q   : 退出")
    print("=" * 50)

    # 串口监听线程（离线测试不需要）
    if not args.test_image and isinstance(comm, SerialComm):
        t = threading.Thread(target=uart_listener, args=(comm, modes),
                             daemon=True)
        t.start()

    try:
        # 主循环
        frame_worker(comm, modes, test_image=args.test_image, no_gui=args.no_gui)
    finally:
        if not args.test_image:
            release_camera()


if __name__ == "__main__":
    main()
