#!/usr/bin/env python3
"""
yolo_tflite.py — TFLite YOLO 推理封装
======================================
从 phase2_yolo_detector/deploy_rpi/infer_rpi.py 移植并封装成类。
使用 tflite-runtime 在 Pi 上轻量推理。

关键（坑 10）：
  - xywh 是归一化 [0,1]（不是像素！）
  - class scores 已是概率 [0,1]（不需要 sigmoid！）
"""

import numpy as np
import cv2

try:
    from ai_edge_litert.interpreter import Interpreter
except ImportError:
    try:
        from tflite_runtime.interpreter import Interpreter
    except ImportError:
        try:
            from tensorflow.lite.python.interpreter import Interpreter
        except ImportError:
            Interpreter = None


def _nms(boxes, iou_thres=0.45):
    """非极大值抑制"""
    if not boxes:
        return []
    sorted_boxes = sorted(boxes, key=lambda b: -b[1])
    keep = []
    while sorted_boxes:
        best = sorted_boxes.pop(0)
        keep.append(best)
        rest = []
        for b in sorted_boxes:
            xa = max(best[2], b[2])
            ya = max(best[3], b[3])
            xb = min(best[4], b[4])
            yb = min(best[5], b[5])
            inter = max(0, xb - xa) * max(0, yb - ya)
            area_b = (b[4] - b[2]) * (b[5] - b[3])
            area_best = (best[4] - best[2]) * (best[5] - best[3])
            union = area_b + area_best - inter
            iou = inter / union if union > 0 else 0
            if iou <= iou_thres:
                rest.append(b)
        sorted_boxes = rest
    return keep


class YOLOTFLite:
    """YOLOv8 TFLite 推理器（单例加载，线程安全）"""

    _instance = None
    _model_path = None

    def __new__(cls, model_path=None, num_threads=4):
        """单例模式：同一模型路径只加载一次"""
        if cls._instance is None or (model_path and model_path != cls._model_path):
            if Interpreter is None:
                raise ImportError("需要 tflite-runtime 或 tensorflow")
            inst = super().__new__(cls)
            inst._load(model_path, num_threads)
            cls._instance = inst
            cls._model_path = model_path
        return cls._instance

    def _load(self, model_path, num_threads):
        """加载 TFLite 模型"""
        self.interp = Interpreter(model_path=model_path, num_threads=num_threads)
        self.interp.allocate_tensors()
        in_details = self.interp.get_input_details()
        self.in_h, self.in_w = in_details[0]["shape"][1:3]
        self.in_idx = in_details[0]["index"]
        out_details = self.interp.get_output_details()
        self.out_idx = out_details[0]["index"]
        print(f"[YOLO] Loaded {model_path} (input={self.in_w}x{self.in_h})")

    def detect(self, frame, conf_thres=0.25, iou_thres=0.45):
        """
        检测数字。

        参数
        ----------
        frame : ndarray (H, W, 3) BGR
        conf_thres : float
        iou_thres : float

        返回
        -------
        detections : list of (cls_id, conf, x1, y1, x2, y2)
            cls_id=0-9, conf=[0,1], bbox 为像素坐标
        """
        img_h, img_w = frame.shape[:2]

        # 预处理：BGR → RGB，resize，/255，expand_dims
        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        resized = cv2.resize(rgb, (self.in_w, self.in_h))
        arr = np.array(resized, dtype=np.float32) / 255.0
        arr = np.expand_dims(arr, 0)

        # 推理
        self.interp.set_tensor(self.in_idx, arr)
        self.interp.invoke()
        out = self.interp.get_tensor(self.out_idx)[0]  # (14, num_anchors)
        out = out.T  # (num_anchors, 14) = [xc, yc, w, h, p0..p9]

        # 后处理（坑 10：xywh 是 [0,1]，class scores 是概率不需要 sigmoid）
        raw = []
        for det in out:
            xc, yc, w, h = det[0], det[1], det[2], det[3]
            class_scores = det[4:]
            cls_id = int(np.argmax(class_scores))
            conf = float(class_scores[cls_id])
            if conf < conf_thres:
                continue
            x1 = max(0.0, min(float(img_w), (xc - w / 2) * img_w))
            y1 = max(0.0, min(float(img_h), (yc - h / 2) * img_h))
            x2 = max(0.0, min(float(img_w), (xc + w / 2) * img_w))
            y2 = max(0.0, min(float(img_h), (yc + h / 2) * img_h))
            raw.append((cls_id, conf, x1, y1, x2, y2))

        return _nms(raw, iou_thres)
