#!/usr/bin/env python3
"""
core.py — 纯算法层（Pi 部署版）
================================
所有视觉检测算法，不包含任何 cv2.imshow / 绘图 / 交互逻辑。
从 /root/yolo/num/test_photos/改造/core.py 移植，yolo_detect 改用 TFLite。

功能清单：
  - A4纸张检测（Canny + 四边形逼近）
  - 透视变换（拉正到标准尺寸 630×891）
  - 距离测量（基于A4像素高度）
  - YOLO 数字检测（TFLite INT8，单例缓存）
  - 正方形检测（角点对法，支持重叠）
"""

import cv2
import numpy as np
import os

from config import (
    A4_W_CM, A4_H_CM, PX_PER_CM, WARP_W, WARP_H,
    ASPECT_RATIO_TOL, FOCAL_LENGTH, INNER_W_CM, INNER_H_CM,
    SIDE_CORRECTION, MIN_SIDE_PX, SQUARE_SIDE_RATIO_MAX,
    SQUARE_ANGLE_TOL, YOLO_MODEL_PATH, YOLO_CONF_THRES,
    YOLO_IOU_THRES, YOLO_NUM_THREADS,
)


# ════════════════════════════════════════════════════════════════
#  几何工具
# ════════════════════════════════════════════════════════════════

def get_dist(p1, p2):
    """计算两点欧氏距离"""
    return np.linalg.norm(np.array(p1, dtype=np.float32) -
                          np.array(p2, dtype=np.float32))


def order_points(pts):
    """排序四边形顶点 → [左上, 右上, 右下, 左下]"""
    rect = np.zeros((4, 2), dtype="float32")
    s = pts.sum(axis=1)
    rect[0] = pts[np.argmin(s)]
    rect[2] = pts[np.argmax(s)]
    diff = np.diff(pts, axis=1)
    rect[1] = pts[np.argmin(diff)]
    rect[3] = pts[np.argmax(diff)]
    return rect


# ════════════════════════════════════════════════════════════════
#  A4纸张检测
# ════════════════════════════════════════════════════════════════

def _check_frame_ratio(pts, tol=ASPECT_RATIO_TOL):
    """验证四边形宽高比是否接近A4比例 (21:29.7)"""
    h_px = (get_dist(pts[0], pts[3]) + get_dist(pts[1], pts[2])) / 2
    w_px = (get_dist(pts[0], pts[1]) + get_dist(pts[2], pts[3])) / 2
    if w_px < 1 or h_px < 1:
        return False
    ratio = h_px / w_px
    expected = A4_H_CM / A4_W_CM
    return abs(ratio - expected) / expected < tol


def detect_a4_by_white(frame):
    """
    用 Canny 边缘检测 + 四边形逼近 定位A4纸边界。

    返回
    -------
    best_pts : ndarray (4, 2) or None
    intermediates : dict
    """
    h_img, w_img = frame.shape[:2]
    min_area = h_img * w_img * 0.02
    max_area = h_img * w_img * 0.95

    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    blurred = cv2.GaussianBlur(gray, (5, 5), 0)

    edges = cv2.Canny(blurred, 30, 120)
    edges = cv2.dilate(edges, np.ones((3, 3), np.uint8), iterations=1)

    cnts, _ = cv2.findContours(edges, cv2.RETR_LIST, cv2.CHAIN_APPROX_SIMPLE)

    candidates = []
    for c in sorted(cnts, key=cv2.contourArea, reverse=True)[:10]:
        area = cv2.contourArea(c)
        if area < min_area:
            break
        if area > max_area:
            continue
        eps = 0.02 * cv2.arcLength(c, True)
        approx = cv2.approxPolyDP(c, eps, True)
        if len(approx) == 4:
            pts = order_points(approx.reshape(4, 2).astype(np.float32))
            if _check_frame_ratio(pts):
                candidates.append((area, pts))

    intermediates = {
        'gray': gray, 'blurred': blurred, 'edges': edges,
        'contours': cnts, 'candidates': candidates,
    }

    if not candidates:
        return None, intermediates

    # 去重 → 选面积最大
    unique = []
    for area, pts in sorted(candidates, key=lambda x: x[0]):
        center = np.mean(pts, axis=0)
        is_dup = False
        for _, upts in unique:
            if np.linalg.norm(center - np.mean(upts, axis=0)) < 30:
                is_dup = True
                break
        if not is_dup:
            unique.append((area, pts))

    unique.sort(key=lambda x: x[0], reverse=True)
    best_pts = unique[0][1]
    intermediates['unique'] = unique
    intermediates['best_pts'] = best_pts
    return best_pts, intermediates


def detect_a4(frame):
    """
    检测A4纸边界（简化入口）。

    返回
    -------
    pts : ndarray (4, 2) or None
    method : str
    intermediates : dict
    """
    pts, intermediates = detect_a4_by_white(frame)
    if pts is not None:
        return pts, "canny", intermediates
    return None, "failed", intermediates


# ════════════════════════════════════════════════════════════════
#  透视变换
# ════════════════════════════════════════════════════════════════

def warp_a4(frame, outer_pts):
    """透视变换到标准尺寸 630×891"""
    dst = np.array([
        [0, 0], [WARP_W - 1, 0],
        [WARP_W - 1, WARP_H - 1], [0, WARP_H - 1]
    ], dtype=np.float32)
    M = cv2.getPerspectiveTransform(outer_pts.astype(np.float32), dst)
    return cv2.warpPerspective(frame, M, (WARP_W, WARP_H))


# ════════════════════════════════════════════════════════════════
#  像素↔厘米换算 & 距离测量
# ════════════════════════════════════════════════════════════════

def get_px_per_cm():
    """返回透视变换后的像素密度（像素/厘米）"""
    raw = (WARP_W / INNER_W_CM + WARP_H / INNER_H_CM) / 2
    return raw / SIDE_CORRECTION


def calc_distance_from_pts(pts):
    """
    根据A4角点计算距离（小孔成像公式）。

    返回
    -------
    distance_cm : float
    h_px : float
    """
    h_px = (get_dist(pts[0], pts[3]) + get_dist(pts[1], pts[2])) / 2
    if h_px < 1:
        return 0, h_px
    distance_cm = (FOCAL_LENGTH * A4_H_CM) / h_px
    return distance_cm, h_px


def calc_focal_length_from_pts(pts, distance_cm):
    """根据已知距离反算焦距（标定用）"""
    h_px = max(get_dist(pts[0], pts[3]), get_dist(pts[1], pts[2]))
    if h_px < 1 or distance_cm <= 0:
        return 0.0
    return (distance_cm * h_px) / A4_H_CM


# ════════════════════════════════════════════════════════════════
#  YOLO 数字检测（TFLite，单例缓存）
# ════════════════════════════════════════════════════════════════

_yolo_model = None


def yolo_detect(frame, model_path=None, conf_thres=None):
    """
    用YOLO检测数字（TFLite推理）。

    返回
    -------
    detections : list of (cls_id, conf, x1, y1, x2, y2)
    """
    global _yolo_model
    if _yolo_model is None:
        from yolo_tflite import YOLOTFLite
        mp = model_path or YOLO_MODEL_PATH
        _yolo_model = YOLOTFLite(mp, num_threads=YOLO_NUM_THREADS)

    ct = conf_thres if conf_thres is not None else YOLO_CONF_THRES
    return _yolo_model.detect(frame, conf_thres=ct, iou_thres=YOLO_IOU_THRES)


# ════════════════════════════════════════════════════════════════
#  边框检测
# ════════════════════════════════════════════════════════════════

def detect_border_width(roi):
    """检测透视变换后ROI中黑框的宽度（像素）"""
    gray = cv2.cvtColor(roi, cv2.COLOR_BGR2GRAY)
    h, w = gray.shape
    _, th = cv2.threshold(gray, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    borders = []
    for y in range(h // 3):
        if np.sum(th[y, :]) / (w * 255) > 0.3:
            borders.append(y); break
    for x in range(w // 3):
        if np.sum(th[:, x]) / (h * 255) > 0.3:
            borders.append(x); break
    for y in range(h - 1, h * 2 // 3, -1):
        if np.sum(th[y, :]) / (w * 255) > 0.3:
            borders.append(h - y); break
    for x in range(w - 1, w * 2 // 3, -1):
        if np.sum(th[:, x]) / (h * 255) > 0.3:
            borders.append(w - x); break
    if not borders:
        return 40
    return max(int(np.mean(borders) * 1.05), 20)


# ════════════════════════════════════════════════════════════════
#  角点分析工具
# ════════════════════════════════════════════════════════════════

def get_corner_angle(p_prev, p_curr, p_next):
    """计算三个轮廓点构成的内角（度）"""
    v1 = np.array(p_prev, dtype=np.float64) - np.array(p_curr, dtype=np.float64)
    v2 = np.array(p_next, dtype=np.float64) - np.array(p_curr, dtype=np.float64)
    n1, n2 = np.linalg.norm(v1), np.linalg.norm(v2)
    if n1 < 1e-6 or n2 < 1e-6:
        return 180.0
    cos_a = np.clip(np.dot(v1, v2) / (n1 * n2), -1.0, 1.0)
    return float(np.degrees(np.arccos(cos_a)))


def is_right_angle(angle, tol=30):
    """判断一个角是否接近直角"""
    return abs(angle - 90.0) <= tol


def angle_bisector_dir(p_prev, p_curr, p_next):
    """计算角平分线的单位方向向量"""
    v1 = np.array(p_prev, dtype=np.float64) - np.array(p_curr, dtype=np.float64)
    v2 = np.array(p_next, dtype=np.float64) - np.array(p_curr, dtype=np.float64)
    n1, n2 = np.linalg.norm(v1), np.linalg.norm(v2)
    if n1 < 1e-6 or n2 < 1e-6:
        return np.array([1.0, 0.0])
    bis = v1 / n1 + v2 / n2
    nb = np.linalg.norm(bis)
    if nb < 1e-6:
        return np.array([-v1[1] / n1, v1[0] / n1])
    return bis / nb


def dirs_perpendicular(d1, d2, tol_deg=30):
    """判断两个方向向量是否近似垂直"""
    return abs(np.dot(d1, d2)) < np.sin(np.radians(90 - tol_deg))


# ════════════════════════════════════════════════════════════════
#  正方形验证 & 重建
# ════════════════════════════════════════════════════════════════

def validate_square(pts, b_inv, min_side, max_side):
    """
    验证四边形是否为有效正方形。
    四重验证：边长范围 + 边长比 + 角度 + 填充率
    """
    if pts is None or len(pts) != 4:
        return False, 0.0
    pts_f = pts.astype(np.float64)

    sides = [get_dist(pts_f[i], pts_f[(i + 1) % 4]) for i in range(4)]
    avg_side = float(np.mean(sides))
    if avg_side < min_side or avg_side > max_side:
        return False, avg_side
    if min(sides) < 1e-6 or max(sides) / min(sides) > SQUARE_SIDE_RATIO_MAX:
        return False, avg_side

    for i in range(4):
        angle = get_corner_angle(
            pts_f[(i - 1) % 4], pts_f[i], pts_f[(i + 1) % 4])
        if not is_right_angle(angle, tol=SQUARE_ANGLE_TOL):
            return False, avg_side

    h, w = b_inv.shape
    mask = np.zeros((h, w), dtype=np.uint8)
    cv2.fillPoly(mask, [pts.astype(np.int32)], 255)
    total = cv2.countNonZero(mask)
    if total < 100:
        return False, avg_side
    black_px = cv2.countNonZero(cv2.bitwise_and(b_inv, mask))
    if black_px / total < 0.50:
        return False, avg_side

    return True, avg_side


def compute_fill_ratio(pts, b_inv):
    """计算四边形区域内黑色像素的填充率"""
    h, w = b_inv.shape
    mask = np.zeros((h, w), dtype=np.uint8)
    cv2.fillPoly(mask, [pts.astype(np.int32)], 255)
    total = cv2.countNonZero(mask)
    if total < 1:
        return 0.0
    black_px = cv2.countNonZero(cv2.bitwise_and(b_inv, mask))
    return black_px / total


def build_square_from_adjacent(pa, pb, side):
    """从相邻直角顶点重建正方形（返回两个候选）"""
    pa = np.array(pa, dtype=np.float64)
    pb = np.array(pb, dtype=np.float64)
    edge = pb - pa
    edge_len = np.linalg.norm(edge)
    if edge_len < 1e-6:
        return None, None
    perp = np.array([-edge[1], edge[0]]) / edge_len
    box_a = np.array([pa, pb, pb + perp * side, pa + perp * side],
                     dtype=np.float64)
    box_b = np.array([pa, pb, pb - perp * side, pa - perp * side],
                     dtype=np.float64)
    return box_a, box_b


# ════════════════════════════════════════════════════════════════
#  核心：重叠正方形检测（角点对法）
# ════════════════════════════════════════════════════════════════

def detect_squares_in_roi(roi):
    """
    检测透视变换后 ROI 内的所有黑色正方形（支持重叠）。

    返回
    -------
    squares : list of dict
        每个元素: {'pts': ndarray(4,2), 'side_px': float}
    """
    # ── Step 1: 预处理 ──
    border_px = detect_border_width(roi)
    gray = cv2.cvtColor(roi, cv2.COLOR_BGR2GRAY)
    h, w = gray.shape

    x1v, y1v = border_px, border_px
    x2v, y2v = w - border_px, h - border_px
    if x2v <= x1v or y2v <= y1v:
        return []

    _, b_inv = cv2.threshold(gray, 0, 255,
                             cv2.THRESH_BINARY_INV + cv2.THRESH_OTSU)
    valid_mask = np.zeros((h, w), dtype=np.uint8)
    valid_mask[y1v:y2v, x1v:x2v] = 255
    b_inv = cv2.bitwise_and(b_inv, valid_mask)

    max_side = min(x2v - x1v, y2v - y1v) * 0.95
    min_side = MIN_SIDE_PX * 0.65

    # ── Step 2: 提取所有轮廓中的角点 ──
    cnts, _ = cv2.findContours(b_inv, cv2.RETR_LIST, cv2.CHAIN_APPROX_SIMPLE)
    all_corners = []
    all_edges = []

    for c in cnts:
        if cv2.contourArea(c) < 400:
            continue
        peri = cv2.arcLength(c, True)
        approx = cv2.approxPolyDP(c, 0.015 * peri, True)
        pts_c = approx.reshape(-1, 2)
        n = len(pts_c)
        if n < 3:
            continue
        base = len(all_corners)
        for k in range(n):
            p_prev = pts_c[(k - 1) % n].astype(np.float64)
            p_curr = pts_c[k].astype(np.float64)
            p_next = pts_c[(k + 1) % n].astype(np.float64)
            angle = get_corner_angle(p_prev, p_curr, p_next)
            bis = angle_bisector_dir(p_prev, p_curr, p_next)
            all_corners.append({
                'pt': p_curr, 'angle': angle, 'bis': bis,
                'is_right': is_right_angle(angle, tol=30),
            })
        for k in range(n):
            all_edges.append((base + k, base + (k + 1) % n))

    # ── Step 3: 相邻直角点对 → 重建候选 ──
    candidates = []
    for (ia, ib) in all_edges:
        ca, cb = all_corners[ia], all_corners[ib]
        if not (ca['is_right'] and cb['is_right']):
            continue
        pa, pb = ca['pt'], cb['pt']
        side = get_dist(pa, pb)
        if side < min_side or side > max_side:
            continue
        if not dirs_perpendicular(ca['bis'], cb['bis'], tol_deg=30):
            continue
        box_a, box_b = build_square_from_adjacent(pa, pb, side)
        for box in [box_a, box_b]:
            if box is not None:
                candidates.append(box)

    # ── Step 4: 验证候选 ──
    valid = []
    for box in candidates:
        try:
            ordered = order_points(box.astype(np.float32))
        except Exception:
            continue
        pts_i = ordered.astype(np.int32)
        if (np.any(pts_i[:, 0] < x1v - 5) or
                np.any(pts_i[:, 0] >= x2v + 5) or
                np.any(pts_i[:, 1] < y1v - 5) or
                np.any(pts_i[:, 1] >= y2v + 5)):
            continue
        ok, side_px = validate_square(pts_i, b_inv, min_side, max_side)
        if ok:
            valid.append({'pts': pts_i, 'side_px': side_px})

    # ── Step 5: 兜底 ──
    cnts_ext, _ = cv2.findContours(b_inv, cv2.RETR_EXTERNAL,
                                   cv2.CHAIN_APPROX_SIMPLE)
    for c in cnts_ext:
        if cv2.contourArea(c) < 800:
            continue
        peri = cv2.arcLength(c, True)
        approx = cv2.approxPolyDP(c, 0.02 * peri, True)
        if len(approx) == 4:
            ordered = order_points(approx.reshape(4, 2).astype(np.float32))
            pts_i = ordered.astype(np.int32)
            ok, side_px = validate_square(pts_i, b_inv, min_side, max_side)
            if ok:
                valid.append({'pts': pts_i, 'side_px': side_px})
        else:
            rect = cv2.minAreaRect(c)
            wr, hr = rect[1]
            if wr < 1 or hr < 1 or min(wr, hr) / max(wr, hr) < 0.75:
                continue
            side_px = (wr + hr) / 2.0
            if side_px < min_side or side_px > max_side:
                continue
            box = cv2.boxPoints(rect)
            ordered = order_points(box.astype(np.float32))
            pts_i = ordered.astype(np.int32)
            ok, side_px2 = validate_square(pts_i, b_inv, min_side, max_side)
            if ok:
                valid.append({'pts': pts_i, 'side_px': side_px2})

    if not valid:
        return []

    # ── Step 6: 去重 ──
    valid.sort(key=lambda x: x['side_px'], reverse=True)
    final = []
    used = [False] * len(valid)
    for i in range(len(valid)):
        if used[i]:
            continue
        ci = np.mean(valid[i]['pts'], axis=0)
        si = valid[i]['side_px']
        for j in range(i + 1, len(valid)):
            if used[j]:
                continue
            if np.linalg.norm(ci - np.mean(valid[j]['pts'], axis=0)) < si * 0.35:
                used[j] = True
        final.append(valid[i])

    return final
