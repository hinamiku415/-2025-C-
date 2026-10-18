# STM32工程说明

## 1. 架构概述

```
┌─────────────────────────────────────────────────────┐
│                    STM32F103C8                       │
│                                                     │
│  ┌──────────────┐    ┌──────────────────────────┐  │
│  │   main.c     │    │   Hardware Drivers        │  │
│  │  (业务逻辑)  │    │                          │  │
│  │              │    │  Serial.c   → USART1底层  │  │
│  │ 状态机       │◄───┤  HMI.c      → 串口屏收发 │  │
│  │ 一键启动     │    │  USART2.c   → 树莓派底层  │  │
│  │ 无线通信     │    │  Pi_Comm.c  → 协议解析   │  │
│  │ OLED显示     │    │  Wireless.c → NRF24L01   │  │
│  └──────────────┘    └──────────────────────────┘  │
└─────────────────────────────────────────────────────┘
```

## 2. 串口分配

| 串口 | 引脚 | 波特率 | 用途 | 协议 |
|------|------|--------|------|------|
| USART1 | PA9/PA10 | 9600 | HMI串口屏 | 单字节状态指令 + 文本参数指令 |
| USART3 | PB10/PB11 | 115200 | 树莓派通信 | 文本协议（见下） |

> **注意：** 代码文件仍命名为 `USART2.c/.h`，内部现在使用 USART3 外设（PB10/PB11）。
> 备用配置（USART2，PA2/PA3）已注释保留在 USART2.c 中，原板 PB11 异常时可切回。
> PA2/PA3 与 NRF24L01 软件SPI 冲突，启用无线模块时不可同时使用 USART2。

## 3. HMI串口屏通信协议（HMI.c）

### 3.1 HMI → STM32（接收）

HMI屏通过USART1向STM32发送两类数据：

**类型1：单字节状态切换指令（HMI按钮事件）**

| 字节值 | 切换到的状态 | 说明 |
|--------|-------------|------|
| `0x00` | STATE_IDLE | 强制复位到空闲 |
| `0x01` | STATE_BASIC_1 | 基本(1)：循迹1米 |
| `0x02` | STATE_BASIC_2 | 基本(2)：静止测量 |
| `0x03` | STATE_BASIC_34 | 基本(3)(4)：前进到D停止测量 |
| `0x11` | STATE_ADV_1 | 发挥(1)：静止测最小正方形 |
| `0x12` | STATE_ADV_2 | 发挥(2)：前进到D3测最小正方形 |
| `0x13` | STATE_ADV_3 | 发挥(3)：前进到D4测指定编号正方形 |
| `0x14` | STATE_ADV_45 | 发挥(4)(5)：倾斜板，前进到D5 |

**类型2：文本参数指令（`\n`结尾，由`Command_Parser()`解析）**

```
D=150\n    → 设置 target_D = 150（目标距离，cm）
N=2\n      → 设置 target_N = 2（目标正方形编号）
```

### 3.2 STM32 → HMI（发送）

使用Nextion格式，`printf` 重定向到 USART1（`fputc` 在 Serial.c 中实现）：

```c
HMI_SetText("D", "120.5cm");   // 更新距离显示
HMI_SetText("X", "008.3cm");   // 更新边长显示
UI_UpdateResult(120.5, 8.3);   // 封装接口
UI_UpdatePower(350, 1750);     // 更新电流/功耗显示
```

### 3.3 API接口

```c
void Command_Parser(void);              // 主循环中调用，解析文本参数指令
void UI_UpdateResult(float d, float x); // 更新D和x显示
void UI_UpdatePower(int ma, int mw);    // 更新电流和功耗显示
```

## 4. 树莓派通信协议（Pi_Comm.c / USART2.c）

### 4.1 STM32 → 树莓派（发送）

```
MODE:0\n    切换到空闲模式
MODE:2\n    切换到形状识别（基本题）
MODE:3\n    切换到多正方形识别（发挥1/2）
MODE:4\n    切换到带编号正方形（发挥3）
MODE:5\n    切换到倾斜板模式（发挥4/5）
TRIGGER\n   触发一次测量
D=xxx\n     设置目标距离
N=x\n       设置目标编号
```

### 4.2 树莓派 → STM32（接收解析）

```
D:120.50                          → distance=120.5
D:120.50,shape:circle,x:8.30      → distance=120.5, shape=circle, size=8.3
D:120.50,N:2,x:7.50               → distance=120.5, num=2, size=7.5
STATUS:mode=3,status=2            → 忽略
ERROR:xxx                         → 忽略
```

### 4.3 数据结构

```c
typedef struct {
    uint8_t     valid;          // 数据是否有效
    float       distance;       // 距离D（cm）
    ShapeType_t shape_type;     // 形状类型（圆/三角/正方形）
    uint8_t     square_num;     // 正方形编号（0=未识别）
    float       size_cm;        // 边长或直径（cm）
} PiData_t;
```

## 5. 主程序（main.c）

### 5.1 初始化流程

```c
App_Init():
  1. 配置SysTick（1ms中断，g_tick计数）
  2. 初始化外设：OLED, Key, NRF24L01, Serial(USART1), PiComm(USART2)
  3. OLED显示启动提示
  4. 向树莓派发送 MODE:0（进入空闲）
```

### 5.2 主循环

```c
while(1):
  1. PiComm_Process()    // 解析树莓派数据
  2. App_HandlePiData()  // 处理测量结果，更新状态机
  3. Command_Parser()    // 解析HMI文本参数指令（D=xxx / N=x）
  4. App_HandleKey()     // 按键1：一键启动
  5. App_HandleWireless()// 处理小车反馈
  6. App_UpdateOLED()    // 刷新OLED（200ms一次）
```

### 5.3 状态机

**状态切换：由HMI屏单字节指令完成（见第3节）**

**按键1（一键启动）：根据当前状态执行对应任务**

| 当前状态 | 动作 |
|---------|------|
| IDLE | 切换到BASIC_2，发送`MODE:2`+`TRIGGER` |
| BASIC_1 | 通知小车出发（循迹1米） |
| BASIC_2 | 发送`MODE:2`+`TRIGGER`（静止测量） |
| BASIC_34 | 发送`MODE:2`+`D=xxx`，通知小车前进到target_D |
| ADV_1 | 发送`MODE:3`+`TRIGGER`（静止测最小正方形） |
| ADV_2 | 发送`MODE:3`+`D=xxx`，小车前进到target_D |
| ADV_3 | 发送`MODE:4`+`N=x`+`D=xxx`，小车前进 |
| ADV_45 | 发送`MODE:5`+`N=x`+`D=xxx`，小车前进（倾斜板） |

**收到树莓派测量数据：**
- 更新全局变量 `g_dist_d`, `g_width_x`
- 调用 `UI_UpdateResult()` 刷新HMI显示
- 所有测量状态收到数据后自动切回 STATE_IDLE

**收到小车反馈（0xAA 0x01）：**
- 延迟200ms等待停稳
- 发送`TRIGGER`触发树莓派测量

## 6. 硬件连接

| STM32引脚 | 连接 | 说明 |
|----------|------|------|
| PA9  | HMI屏 RX | USART1 TX |
| PA10 | HMI屏 TX | USART1 RX |
| PA2  | USB转TTL RX | USART2 TX → 树莓派 |
| PA3  | USB转TTL TX | USART2 RX ← 树莓派 |
| PA7  | 按键1 | 一键启动，低电平触发 |
| GND  | 共地 | 必须连接 |

**NRF24L01 引脚（软件SPI，占用PA口）：**

| STM32引脚 | NRF24L01 | 说明 |
|----------|----------|------|
| PA0 | CE  | 片选使能 |
| PA1 | CSN | SPI片选 |
| PA2 | SCK | SPI时钟 |
| PA3 | MOSI | SPI数据输出 |
| PA4 | MISO | SPI数据输入 |

**注意：** USB转TTL模块的TX接STM32的RX（PB11），RX接STM32的TX（PB10）。

## 7. 典型使用流程

### 基本(2) - 静止测量

```
1. HMI屏按按钮 → 发送 0x02 → STM32切换到 STATE_BASIC_2
2. 按STM32上的物理按键1（一键启动）
   → STM32发送: MODE:2\n
   → STM32发送: TRIGGER\n
3. 树莓派测量并回复: D:120.50,shape:square,x:8.30\n
4. STM32解析数据，HMI显示 D=120.5cm, X=008.3cm
5. 自动切回 STATE_IDLE
```

### 基本(3) - 小车前进到D1停止

```
1. HMI屏输入 D=150\n → target_D = 150
2. HMI屏按按钮 → 发送 0x03 → STM32切换到 STATE_BASIC_34
3. 按物理按键1启动
   → STM32发送: MODE:2\n, D=150\n
   → NRF无线发送距离150给小车
4. 小车前进到150cm停止，反馈 0xAA 0x01
5. STM32延迟200ms → 发送: TRIGGER\n
6. 树莓派回复: D:150.20,shape:circle,x:12.40\n
7. HMI显示结果，自动切回 STATE_IDLE
```

### 发挥(3) - 指定编号正方形

```
1. HMI屏输入 D=140\n, N=2\n
2. HMI屏按按钮 → 发送 0x13 → STM32切换到 STATE_ADV_3
3. 按物理按键1启动
   → STM32发送: MODE:4\n, N=2\n, D=140\n
   → NRF无线发送距离140给小车
4. 小车前进到140cm停止，反馈 0xAA 0x01
5. STM32发送: TRIGGER\n
6. 树莓派回复: D:140.10,N:2,x:7.50\n
7. HMI显示结果
```

## 8. 编译配置

Keil工程需包含以下文件：

```
Hardware/Serial.c
Hardware/HMI.c
Hardware/USART2.c
Hardware/Pi_Comm.c
Hardware/Wireless.c
Hardware/NRF24L01.c
Hardware/Key.c
Hardware/OLED.c
Hardware/AD.c
```

包含路径需包含 `Hardware/` 目录。

## 9. 调试方法

### 9.1 测试HMI状态切换

用串口助手（9600bps）连接USART1，发送单字节 `0x02`，STM32应切换到 STATE_BASIC_2（OLED第1行显示 "Mode: Basic 2"）。

发送文本 `D=150\n`，`target_D` 应变为150（OLED第2行显示 "Target D:150cm"）。

### 9.2 测试USART3通信（树莓派）

用串口助手（115200bps）连接 PB10/PB11，发送：
```
D:120.50,shape:square,x:8.30\n
```
STM32应解析出数据并更新HMI显示。

## 10. 待完善

- [ ] ADC电流采样（发挥第6项：实时监测供电电流Is，显示P和Pmax）
- [ ] 超时检测（发送TRIGGER后5s未收到数据，报错提示）
- [ ] 数据校验（CRC或简单校验和）


---

## 11. 无线通信协议（NRF24L01）

### 11.1 帧格式（4字节固定长度）

| 帧头1 | 帧头2 | 数据1 | 数据2 | 说明 |
|--------|--------|-------|-------|------|
| 0xAA | 0xBB | 0xFF | 0x00 | 重置指令：小车清零位置和PID积分 |
| 0xAA | 0xBB | 距离低 | 距离高 | 目标距离（cm），小车用编码器PID跑到此距离 |
| 0xAA | 0xCC | 距离低 | 距离高 | 实时距离（cm），树莓派测的，小车用作PID反馈 |
| 0xAA | 0xBB | 0x01 | 0x00 | 小车到达反馈（小车→发射端） |

### 11.2 发射端引脚（NRF24L01 软件SPI）

| 信号 | 引脚 | 说明 |
|------|------|------|
| CE   | PA0  | 片选使能 |
| CSN  | PA1  | SPI片选 |
| SCK  | PB14 | SPI时钟（原PA2，已迁移避免与USART2冲突） |
| MOSI | PB15 | SPI数据输出（原PA3，已迁移） |
| MISO | PA4  | SPI数据输入 |

### 11.3 小车端引脚

| 信号 | 引脚 |
|------|------|
| CE   | PA4  |
| CSN  | PA5  |
| SCK  | PC4  |
| MOSI | PC5  |
| MISO | PB0  |

### 11.4 通信参数

- 地址：`{0x11, 0x22, 0x11, 0x22, 0x55}`（收发相同）
- 频道：2.402GHz（RF_CH=0x02）
- 速率：2Mbps
- 数据包宽度：4字节
- 自动应答：开启，重传3次，间隔250us

---

## 12. 完整流程（基本3/4 + 发挥2/3/4/5）

### 12.1 一键启动后的完整时序

```
用户操作：HMI输入距离D → 切换状态 → 按KEY1
    ↓
STM32 App_HandleKey()：
    1. PiComm_SendMode(2/3/4/5)  → 树莓派记住pending_mode，切到模式1实时测距
    2. PiComm_SendTargetDist(D)  → 树莓派记住目标距离
    3. Wireless_SendDistance(D)  → 先发重置帧清零小车位置，再发目标距离
    ↓
树莓派后台线程（模式1，15fps）：
    持续发送 D:xxx.xx\n → STM32 收到
    ↓
STM32 App_HandlePiData()：
    每100ms调用 Wireless_SendActualDistance(D) → 发 [0xAA][0xCC][D低][D高] 给小车
    ↓
小车（按KEY2启动 Car_state2=1）：
    TIM1中断(10ms)：Target_Location_arrive_D(Target, Actual_Location_D)
    PID: error = Target - Actual_Location_D → 驱动电机
    ↓
小车到达（|Actual_Location_D - Target| ≤ 0.3cm 持续500ms）：
    Car_state2 = 2 → 蜂鸣器响 → NRF发送 [0xAA][0xBB][0x01][0x00]
    ↓
STM32 App_HandleWireless()（主循环轮询）：
    Wireless_ReceiveFeedback() 收到 0x01
    → Delay_ms(200)
    → PiComm_SendTrigger()
    ↓
树莓派 _uart_listener 收到 TRIGGER：
    切换 current_mode = pending_mode（2/3/4/5）
    → modes.trigger() 拍照测量
    → _send_mode_info() 发送结果
    → comm.send_status(mode, 2)
    ↓
STM32 收到测量结果 → 更新 OLED + HMI 串口屏
```

### 12.2 无线反馈调用链

```
小车 TIM1 中断（10ms）
    → Control_kick() 检测到达
    → Car_state2 = 2
    → NRF24L01_Send([0xAA][0xBB][0x01][0x00])

发射端主循环（每次循环）
    → App_HandleWireless()
    → Wireless_ReceiveFeedback()
    → NRF24L01_Receive()（轮询，非中断）
    → 收到 → 返回 feedback_data[0]=0x01
    → 发 TRIGGER 给树莓派
```

### 12.3 树莓派 pending_mode 机制

**问题：** 题目要求"启动后不得再对装置进行其他操作"，不能等小车到达后再手动切换模式。

**解决：** 按键时一次性发 `MODE:x`（x=2/3/4/5），树莓派收到后：
- `current_mode = 1`（立刻切到实时测距，给小车导航）
- `pending_mode = x`（记住目标模式）
- 收到 `TRIGGER` 时：`current_mode = pending_mode`，执行测量

这样一键启动后全自动，无需人工干预。


---

## 13. 蓝牙方案（JDY-31，替代 NRF24L01）

### 13.1 方案背景

NRF24L01 存在以下问题导致不可靠：
- SPI 通信受电机振动和电源噪声干扰，芯片频繁进入掉电模式（状态3）
- 半双工限制，持续发送时小车接收不稳定
- 发射端发停车指令时小车可能收不到，导致过冲严重

**新方案：树莓派通过 JDY-31 蓝牙模块直接控制小车，不经过 STM32 中转。**

### 13.2 硬件连接

**小车端（STM32F103VET6）：**

| JDY-31 引脚 | STM32 引脚 | 说明 |
|-------------|-----------|------|
| TXD | PC11 (UART4 RX) | 蓝牙→小车 |
| RXD | PC10 (UART4 TX) | 小车→蓝牙 |
| VCC | 3.3V | 供电 |
| GND | GND | 共地 |

**树莓派端：** 使用内置蓝牙，通过 RFCOMM Socket 连接 JDY-31。

### 13.3 蓝牙通信协议

| 指令 | 方向 | 说明 |
|------|------|------|
| `GO\r\n` | 树莓派→小车 | 启动前进（Car_state2=1） |
| `STOP\r\n` | 树莓派→小车 | 停车（反向制动200ms+停） |
| `TARGET:xxx\r\n` | 树莓派→小车 | 设置编码器目标距离（基础1用） |

### 13.4 完整流程（基础3/4，发挥2/3/4/5）

```
STM32 按键 → 发 MODE:2/3/4/5 给树莓派（串口）
    ↓
树莓派 _uart_listener 收到：
    1. pending_mode = 2/3/4/5（记住目标测量模式）
    2. current_mode = 1（切到实时测距）
    3. bt.send_go()（蓝牙发 GO 给小车）
    ↓
小车收到 "GO" → Car_state2=1，匀速前进+循迹
    ↓
树莓派 _headless_worker 持续测距（60fps）：
    - 发 D:xxx 给 STM32（串口，用于显示）
    - 监控：当 D ≤ target_D + 15cm → bt.send_stop()
    ↓
小车收到 "STOP" → 反向制动200ms → 停车
    ↓
树莓派继续测距，检测小车是否停稳：
    - 连续5帧距离变化 < 0.5cm → 确认停稳
    ↓
树莓派切换到 pending_mode，触发测量：
    - modes.trigger() → _send_mode_info() → 发结果给 STM32
    ↓
STM32 显示结果到 OLED + HMI 串口屏
```

### 13.5 关键参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 提前量 | target_D + 15cm | 补偿蓝牙延迟+刹车距离 |
| 停稳判断 | 连续5帧 ΔD < 0.5cm | 约250ms（60fps×5帧） |
| 小车速度 | PID目标速度=10 | 慢速前进减少过冲 |
| 蓝牙波特率 | 9600bps | JDY-31 默认 |
| 蓝牙延迟 | ~20ms | 比 NRF24L01 大但更稳定 |

### 13.6 STM32 发射端改动

- `ENABLE_WIRELESS` 关闭（NRF24L01 不再使用）
- `App_HandleKey` 里只发 MODE/D/N 给树莓派，不发无线指令
- 小车控制完全由树莓派负责

### 13.7 树莓派代码文件

| 文件 | 说明 |
|------|------|
| `pro/bt_comm.py` | 蓝牙通信类（连接/发送/自动重连） |
| `pro/main.py` | _headless_worker 里的停车监控逻辑 |
| `pro/config.py` | BT_MAC_ADDRESS / BT_CHANNEL 配置 |

### 13.8 小车代码改动

| 文件 | 说明 |
|------|------|
| `car/Hardware/usart.c` | 新增 uart4_init()（UART4，PC10/PC11，9600bps） |
| `car/Hardware/usart.h` | 新增 uart4 相关函数声明 |
| `car/System/sys.c` | SYS_Init() 里加 uart4_init(9600) |
| `car/User/main.c` | NRF 接收替换为蓝牙 UART 接收（GO/STOP/TARGET） |
| `car/Hardware/PID.c` | Target_Location_arrive_D 里 Car_state 改为 Car_state2 |

### 13.9 实测结果

- 目标150cm，停车触发在 D≈193cm（提前15cm），实际停在 148~150cm ✅
- 停稳检测正常，测量结果准确
- 蓝牙连接稳定，不再出现 NRF24L01 的掉电问题
