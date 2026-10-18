#!/usr/bin/env python3
"""
modes.py — 6 种工作模式
=========================
从比赛代码 modes.py 移植结构，算法调 core.py。
"""

import cv2
import numpy as np
import time

from config import (
    WARP_W, WARP_H, MEASURE_FRAMES, MEASURE_INTERVAL_MS,
    TILT_MIN_ANGLE, TILT_MAX_ANGLE, YOLO_SKIP_FULLFRAME,
)
import core


def _merge_multi_frame_info(infos, mode):
    """
    合并多帧测量结果，对距离和边长取中位数。
    """
    if not infos:
        return None

    distances = [info.get('distance', 0) for info in infos]
    merged_d = float(np.median(distances))

    if mode == 2:
        shapes = infos[0].get('shapes', [])
        for i, shape in enumerate(shapes):
            side_vals = []
            for info in infos:
                s_list = info.get('shapes', [])
                if i < len(s_list):
                    if 'side' in s_list[i]:
                        side_vals.append(s_list[i]['side'])
                    elif 'diameter' in s_list[i]:
                        side_vals.append(s_list[i]['diameter'])
            if side_vals:
                if 'side' in shape:
                    shape['side'] = float(np.median(side_vals))
                elif 'diameter' in shape:
                    shape['diameter'] = float(np.median(side_vals))
        return {'distance': merged_d, 'shapes': shapes}

    if mode == 3:
        min_sides = [info.get('min_side', 0) for info in infos
                     if info.get('min_side', 0) > 0]
        merged_min = float(min(min_sides)) if min_sides else 0
        return {'distance': merged_d, 'squares': [], 'min_side': merged_min}

    # 模式 4/5：按编号聚合
    all_squares_by_num = {}
    for info in infos:
        for sq in info.get('squares', []):
            num = sq.get('number', '?')
            if num not in all_squares_by_num:
                all_squares_by_num[num] = []
            all_squares_by_num[num].append(sq['side'])

    merged_squares = []
    for num, sides in all_squares_by_num.items():
        merged_squares.append({
            'number': num,
            'side': float(np.median(sides)),
        })

    min_side = min((sq['side'] for sq in merged_squares), default=0)
    return {
        'distance': merged_d,
        'squares': merged_squares,
        'min_side': min_side,
    }


class TiltCorrector:
    """倾斜矫正器 — 用于模式5（物面倾斜30~60度）"""

    def estimate_tilt_angle(self, outer_pts):
        """
        从四边形几何估计倾斜角度。
        利用透视缩短效应：近端边长 > 远端边长。
        """
        if outer_pts is None or len(outer_pts) != 4:
            return None
        top_len = core.get_dist(outer_pts[0], outer_pts[1])
        bottom_len = core.get_dist(outer_pts[3], outer_pts[2])
        if top_len < 1 or bottom_len < 1:
            return None
        ratio = min(top_len, bottom_len) / max(top_len, bottom_len)
        ratio = np.clip(ratio, -1.0, 1.0)
        angle_deg = float(np.degrees(np.arccos(ratio)))
        if angle_deg < TILT_MIN_ANGLE or angle_deg > TILT_MAX_ANGLE:
            return None
        return angle_deg

    def apply_tilt_correction(self, warped, tilt_angle):
        """对已透视变换的图像施加二次矫正"""
        h, w = warped.shape[:2]
        cos_a = np.cos(np.radians(tilt_angle))
        src = np.array([
            [0, 0], [w - 1, 0],
            [w - 1, h - 1], [0, h - 1]
        ], dtype=np.float32)
        inset = (w * (1 - cos_a)) / 2
        dst = np.array([
            [inset, 0], [w - 1 - inset, 0],
            [w - 1, h - 1], [0, h - 1]
        ], dtype=np.float32)
        M = cv2.getPerspectiveTransform(src, dst)
        return cv2.warpPerspective(warped, M, (w, h))

    def get_corrected_px_per_cm(self, base_px_per_cm, tilt_angle):
        """计算倾斜补偿后的 px_per_cm"""
        cos_a = np.cos(np.radians(tilt_angle))
        if cos_a < 0.01:
            return base_px_per_cm
        return base_px_per_cm / cos_a


# ── 中值滤波缓冲区 ──
_dist_buffer = []


class Modes:
    def __init__(self):
        self._frozen = {}
        self._tilt = TiltCorrector()

    def process(self, frame, outer_pts, mode):
        """
        每帧调用。模式1返回测距结果，其他模式返回冻结结果。
        """
        if mode == 1:
            return self.mode_1(frame, outer_pts)
        if mode in self._frozen:
            return self._frozen[mode]
        blank = core.warp_a4(frame, outer_pts) if outer_pts is not None \
                else np.zeros((WARP_H, WARP_W, 3), dtype=np.uint8)
        return blank, None

    def trigger(self, frame, outer_pts, mode, capture_fn=None):
        """
        触发一次测量（模式2-6）。
        若提供 capture_fn，则拍 MEASURE_FRAMES 帧取中位数。
        """
        if outer_pts is None:
            return None, None
        fn = {2: self.mode_2, 3: self.mode_3, 4: self.mode_4,
              5: self.mode_5, 6: self.mode_6}
        if mode not in fn:
            return None, None

        if capture_fn is None or MEASURE_FRAMES <= 1:
            roi, info = fn[mode](frame, outer_pts)
            self._frozen[mode] = (roi, info)
            return roi, info

        # 多帧采集
        all_infos = []
        last_roi = None
        last_outer = outer_pts

        for i in range(MEASURE_FRAMES):
            f = capture_fn()
            pts, _, _ = core.detect_a4(f)
            if pts is None:
                pts = last_outer
            else:
                last_outer = pts
            try:
                r, info = fn[mode](f, pts)
                if info:
                    all_infos.append(info)
                    last_roi = r
            except Exception as e:
                print(f"Multi-frame error {i}: {e}")
            time.sleep(MEASURE_INTERVAL_MS / 1000.0)

        if not all_infos:
            return None, None

        merged = _merge_multi_frame_info(all_infos, mode)
        self._frozen[mode] = (last_roi, merged)
        return last_roi, merged

    def clear_frozen(self, mode=None):
        if mode is None:
            self._frozen.clear()
        else:
            self._frozen.pop(mode, None)

    # ── 模式 1：实时测距 ──

    def mode_1(self, frame, outer_pts):
        """模式1：实时测距"""
        roi = core.warp_a4(frame, outer_pts)
        d, _ = core.calc_distance_from_pts(outer_pts)
        _dist_buffer.append(d)
        if len(_dist_buffer) > 15:
            _dist_buffer.pop(0)
        stable_d = float(np.median(_dist_buffer))
        return roi, {'distance': stable_d}

    # ── 模式 2：形状识别 ──

    def mode_2(self, frame, outer_pts):
        """模式2：基础目标物（圆/三角/正方形）识别+测距"""
        roi = core.warp_a4(frame, outer_pts)
        d, _ = core.calc_distance_from_pts(outer_pts)
        px_per_cm = core.get_px_per_cm()

        gray = cv2.cvtColor(roi, cv2.COLOR_BGR2GRAY)
        _, b_inv = cv2.threshold(gray, 0, 255,
                                 cv2.THRESH_BINARY_INV + cv2.THRESH_OTSU)
        border_px = core.detect_border_width(roi)
        mask = np.zeros_like(b_inv)
        mask[border_px:WARP_H - border_px, border_px:WARP_W - border_px] = 255
        b_inv = cv2.bitwise_and(b_inv, mask)

        cnts, _ = cv2.findContours(b_inv, cv2.RETR_EXTERNAL,
                                   cv2.CHAIN_APPROX_SIMPLE)
        shapes = []
        for c in cnts:
            area = cv2.contourArea(c)
            if area < 400:
                continue
            peri = cv2.arcLength(c, True)
            approx = cv2.approxPolyDP(c, 0.02 * peri, True)
            n = len(approx)

            if n == 3:
                # 三角形
                sides = [core.get_dist(approx[i], approx[(i + 1) % 3])
                         for i in range(3)]
                avg_side = float(np.mean(sides))
                if max(sides) / min(sides) < 1.15:
                    shapes.append({
                        'type': 'triangle',
                        'side': avg_side / px_per_cm,
                    })
            elif n == 4:
                # 正方形
                sides = [core.get_dist(approx[i], approx[(i + 1) % 4])
                         for i in range(4)]
                avg_side = float(np.mean(sides))
                if max(sides) / min(sides) < 1.15:
                    shapes.append({
                        'type': 'square',
                        'side': avg_side / px_per_cm,
                    })
            else:
                # 圆形
                circularity = 4 * np.pi * area / (peri * peri)
                if circularity > 0.78:
                    _, radius = cv2.minEnclosingCircle(c)
                    shapes.append({
                        'type': 'circle',
                        'diameter': (radius * 2) / px_per_cm,
                    })

        return roi, {'distance': d, 'shapes': shapes}

    # ── 模式 3：多正方形最小边长 ──

    def mode_3(self, frame, outer_pts):
        """模式3：检测所有正方形，返回最小边长"""
        roi = core.warp_a4(frame, outer_pts)
        d, _ = core.calc_distance_from_pts(outer_pts)
        px_per_cm = core.get_px_per_cm()

        squares = core.detect_squares_in_roi(roi)
        min_side = 0
        if squares:
            min_side = min(sq['side_px'] / px_per_cm for sq in squares)

        return roi, {
            'distance': d,
            'squares': [],
            'min_side': min_side,
        }

    # ── 模式 4：带编号正方形 ──

    def mode_4(self, frame, outer_pts):
        """模式4：正方形检测 + YOLO数字识别 + 中心配对"""
        roi = core.warp_a4(frame, outer_pts)
        d, _ = core.calc_distance_from_pts(outer_pts)
        px_per_cm = core.get_px_per_cm()

        squares = core.detect_squares_in_roi(roi)
        if YOLO_SKIP_FULLFRAME and not squares:
            # 没有检测到正方形，直接跳过耗时300ms的YOLO，实现实时响应
            return roi, {
                'distance': d,
                'squares': [],
                'detections': [],
            }

        detections = core.yolo_detect(roi)

        result_squares = []
        for sq in squares:
            side_cm = sq['side_px'] / px_per_cm
            cx = float(np.mean(sq['pts'][:, 0]))
            cy = float(np.mean(sq['pts'][:, 1]))

            # 配对：找最近的 YOLO 框
            best_digit = "?"
            best_dist = sq['side_px'] * 0.6
            for cls_id, conf, x1, y1, x2, y2 in detections:
                ycx, ycy = (x1 + x2) / 2, (y1 + y2) / 2
                dist = np.linalg.norm(np.array([cx, cy]) - np.array([ycx, ycy]))
                if dist < best_dist:
                    best_dist = dist
                    best_digit = str(cls_id)

            result_squares.append({
                'number': best_digit,
                'side': side_cm,
                'pts': sq['pts'],
            })

        return roi, {
            'distance': d,
            'squares': result_squares,
            'detections': detections,
        }

    # ── 模式 5：倾斜板矫正 ──

    def mode_5(self, frame, outer_pts):
        """模式5：倾斜板矫正 + 模式4流程"""
        tilt_angle = self._tilt.estimate_tilt_angle(outer_pts)
        if tilt_angle is None:
            roi = core.warp_a4(frame, outer_pts)
            return roi, {'distance': 0, 'squares': [], 'tilt_error': True}

        roi = core.warp_a4(frame, outer_pts)
        corrected = self._tilt.apply_tilt_correction(roi, tilt_angle)
        d, _ = core.calc_distance_from_pts(outer_pts)
        px_per_cm = core.get_px_per_cm()
        px_per_cm_corr = self._tilt.get_corrected_px_per_cm(px_per_cm, tilt_angle)

        squares = core.detect_squares_in_roi(corrected)
        if YOLO_SKIP_FULLFRAME and not squares:
            return corrected, {
                'distance': d,
                'squares': [],
                'detections': [],
                'tilt_angle': tilt_angle,
            }

        detections = core.yolo_detect(corrected)

        result_squares = []
        for sq in squares:
            side_cm = sq['side_px'] / px_per_cm_corr
            cx = float(np.mean(sq['pts'][:, 0]))
            cy = float(np.mean(sq['pts'][:, 1]))

            best_digit = "?"
            best_dist = sq['side_px'] * 0.6
            for cls_id, conf, x1, y1, x2, y2 in detections:
                ycx, ycy = (x1 + x2) / 2, (y1 + y2) / 2
                dist = np.linalg.norm(np.array([cx, cy]) - np.array([ycx, ycy]))
                if dist < best_dist:
                    best_dist = dist
                    best_digit = str(cls_id)

            result_squares.append({
                'number': best_digit,
                'side': side_cm,
                'pts': sq['pts'],
            })

        return corrected, {
            'distance': d,
            'squares': result_squares,
            'detections': detections,
            'tilt_angle': tilt_angle,
        }

    # ── 模式 6：焦距标定 ──

    def mode_6(self, frame, outer_pts, distance_cm=None):
        """
        模式6：焦距标定。
        需要已知距离 distance_cm（由 main.py 通过串口命令传入）。
        """
        roi = core.warp_a4(frame, outer_pts)
        if distance_cm is None or distance_cm <= 0:
            return roi, {'error': 'need distance_cm'}

        new_f = core.calc_focal_length_from_pts(outer_pts, distance_cm)
        if new_f <= 0:
            return roi, {'error': 'calculation failed'}

        # 更新全局焦距
        import config
        config.FOCAL_LENGTH = new_f
        # 同时更新 core 模块引用的值
        core.FOCAL_LENGTH = new_f

        return roi, {
            'focal_length': new_f,
            'distance': distance_cm,
        }
