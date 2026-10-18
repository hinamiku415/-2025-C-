# Pi 实时视觉测量系统

仿照比赛代码架构，使用 TFLite INT8 推理，在树莓派 4B 上运行实时视觉测量。

## 架构

```
main.py          ← 入口：摄像头 + 帧处理 + 全屏显示 + 串口监听
config.py        ← 全局配置
modes.py         ← 6 种模式（测距/形状/正方形/编号/倾斜/标定）
core.py          ← 纯算法层（A4检测 + 正方形检测 + YOLO）
yolo_tflite.py   ← TFLite 推理封装（单例加载）
serial_comm.py   ← 串口通信
viz.py           ← 可视化绘制
```

## 6 种模式

| 模式 | 功能 | 输出 |
|------|------|------|
| 0 | IDLE | 无 |
| 1 | 实时测距 | `D:123.45` |
| 2 | 形状识别 | `D:123.45,shape:square,x:8.30` |
| 3 | 多正方形最小边长 | `D:123.45,x:5.20` |
| 4 | 带编号正方形 | `D:123.45,N:2,x:7.50` |
| 5 | 倾斜板矫正 | 同模式4 |
| 6 | 焦距标定 | `CALIB:F:1700.50,D:100.00` |

## Pi 安装

```bash
# 1. 安装系统依赖
sudo apt update
sudo apt install python3-picamera2 python3-opencv

# 2. 创建虚拟环境
python3 -m venv ~/pi_deploy_env
source ~/pi_deploy_env/bin/activate

# 3. 安装 Python 依赖
pip install -r requirements.txt

# 4. 复制模型文件
cp /path/to/yolov8n_phase2_int8.tflite ~/pi_deploy/deploy_rpi/
```

## 运行

```bash
cd /path/to/pi_deploy
python main.py                      # 正常运行
python main.py --no-serial          # 不连串口（调试用）
python main.py --test-image 1.jpg   # 离线图片测试
```

## 按键操作

| 按键 | 作用 |
|------|------|
| `1`-`6` | 切换模式 |
| `t` | 触发测量（模式2-6） |
| `q` | 退出 |

## 串口协议（文本格式，支持 XOR 校验防干扰）

在 `config.py` 中设置 `USE_CHECKSUM = True`（默认开启）：
- **发送格式**: `$数据*CS\r\n`（例如 `$D:123.45,N:2,x:7.50*4E\r\n`，`CS` 为数据内容的 2 位十六进制 XOR 校验和）
- **接收格式**: 自动兼容 `$命令*CS\r\n` 和原始 `命令\n`，若带校验和且校验失败则自动丢弃防电机误动作。
- 若单片机端暂未实现校验，只需在 `config.py` 中设置 `USE_CHECKSUM = False` 即可切换回纯文本模式。

### Pi → STM32

| 格式 | 纯文本示例 | 带校验包示例 | 用途 |
|------|-----------|------------|------|
| `D:{distance}` | `D:123.45` | `$D:123.45*3B\r\n` | 模式1距离 |
| `D:{d},shape:{type},x:{size}` | `D:123.45,shape:square,x:8.30` | `$D:...*CS\r\n` | 模式2 |
| `D:{d},N:{num},x:{side}` | `D:123.45,N:2,x:7.50` | `$D:...*CS\r\n` | 模式4/5 |
| `D:{d},x:{min_side}` | `D:123.45,x:5.20` | `$D:...*CS\r\n` | 模式3 |
| `STATUS:mode={m},status={s}` | `STATUS:mode=3,status=2` | `$STATUS:...*CS\r\n` | 状态 |
| `ERROR:{msg}` | `ERROR:N=2 not found` | `$ERROR:...*CS\r\n` | 错误 |
| `CALIB:F:{f},D:{d}` | `CALIB:F:1700.50,D:100.00` | `$CALIB:...*CS\r\n` | 标定 |

### STM32 → Pi

| 命令 | 作用 |
|------|------|
| `TRIGGER` 或 `$TRIGGER*34\r\n` | 立即触发测量 |
| `MODE:n` | 切换模式 (0-6) |
| `N=数字` | 指定目标编号 |
| `D=距离` | 指定目标距离 |

## 配置修改

编辑 `config.py` 修改所有参数：

- `SERIAL_PORT`：串口设备（默认 `/dev/ttyUSB0`）
- `FOCAL_LENGTH`：焦距（需标定）
- `RESOLUTION`：摄像头分辨率
- `YOLO_MODEL_PATH`：模型文件路径
- `FULLSCREEN`：是否全屏显示
