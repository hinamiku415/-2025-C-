# TJC/Nextion 串口屏通用库

一个轻量级、跨平台的 TJC/Nextion 串口屏控制库，方便在各种单片机项目中快速集成串口屏功能。

## 特性

- 平台无关：核心库不依赖任何 MCU 头文件
- 轻量级：适合资源受限的嵌入式系统
- 双向通信：支持发送指令和接收事件
- 统一协议：使用 A5...5A 帧协议，结构清晰
- 事件驱动：通过回调函数处理屏幕事件

## 目录结构

```text
TJC_HMI_Lib/
├── Core/
│   ├── tjc_hmi.c           # 核心逻辑实现
│   ├── tjc_hmi.h           # 对外主接口
│   ├── tjc_hmi_config.h    # 用户配置文件
│   ├── tjc_hmi_port.h      # 平台适配接口声明
│   └── tjc_hmi_types.h     # 数据类型定义
├── Port/
│   ├── tjc_hmi_port_stm32f10x.c    # STM32F103 标准库适配
│   └── tjc_hmi_port_stm32f10x.h    # STM32F103 标准库适配头文件
├── README.md
└── PLAN.md
```

## 快速开始

### 1. 包含头文件

```c
#include "tjc_hmi.h"
```

### 2. 初始化

```c
// 初始化串口屏，波特率 9600
TJC_HMI_Init(9600);
```

### 3. 注册事件回调

```c
// 用户自己定义 ID 含义
#define MY_ID_DISTANCE    1
#define MY_ID_NUMBER      2
#define MY_ID_BUTTON_OK   0x01

static void HMI_EventHandler(const TJC_Event_t *event)
{
    switch (event->type) {
        case TJC_EVENT_BUTTON:
            // 按钮事件：event->id 是按钮编号
            if (event->id == MY_ID_BUTTON_OK) {
                // 处理确认按钮
            }
            break;

        case TJC_EVENT_DATA:
            // 数据事件：event->id 是数据类型，event->value 是数值
            if (event->id == MY_ID_DISTANCE) {
                target_D = event->value;  // 距离
            } else if (event->id == MY_ID_NUMBER) {
                target_N = event->value;  // 编号
            }
            break;

        default:
            break;
    }
}

// 注册回调
TJC_HMI_RegisterCallback(HMI_EventHandler);
```

### 4. 发送指令显示内容

```c
// 设置文本
TJC_HMI_SetText("t0", "Hello");
TJC_HMI_SetText("D", "120.5cm");

// 设置数字
TJC_HMI_SetNumber("n0", 123);

// 设置浮点数（转为文本显示）
TJC_HMI_SetFloatText("D", 120.5f, 1);

// 切换页面
TJC_HMI_Page("main");

// 显示/隐藏控件
TJC_HMI_Show("t0");
TJC_HMI_Hide("t0");

// 设置控件属性
TJC_HMI_SetValue("j0", "val", 50);      // 进度条
TJC_HMI_SetValue("t0", "pco", 63488);   // 文字颜色
```

## API 参考

### 初始化

| 函数 | 说明 |
|------|------|
| `TJC_HMI_Init(baudrate)` | 初始化库和串口硬件 |
| `TJC_HMI_RegisterCallback(cb)` | 注册事件回调函数 |

### 发送指令

| 函数 | 说明 |
|------|------|
| `TJC_HMI_SendCmd(cmd)` | 发送原始指令（自动追加 FF FF FF） |
| `TJC_HMI_SendRaw(data, len)` | 发送原始数据 |
| `TJC_HMI_SendEnd()` | 发送结束符（FF FF FF） |

### 控件操作

| 函数 | 说明 |
|------|------|
| `TJC_HMI_SetText(obj, text)` | 设置文本控件内容 |
| `TJC_HMI_SetNumber(obj, num)` | 设置数字控件值 |
| `TJC_HMI_SetFloatText(obj, val, dec)` | 设置浮点数显示 |
| `TJC_HMI_Page(page)` | 切换页面（按名称） |
| `TJC_HMI_PageId(id)` | 切换页面（按 ID） |
| `TJC_HMI_SetValue(obj, attr, val)` | 设置数值属性 |
| `TJC_HMI_SetString(obj, attr, val)` | 设置字符串属性 |
| `TJC_HMI_Show(obj)` | 显示控件 |
| `TJC_HMI_Hide(obj)` | 隐藏控件 |
| `TJC_HMI_ClearText(obj)` | 清空文本控件 |

### 系统指令

| 函数 | 说明 |
|------|------|
| `TJC_HMI_SetBkcmd(mode)` | 设置指令返回模式 |

### 接收处理

| 函数 | 说明 |
|------|------|
| `TJC_HMI_RxByte(byte)` | 喂入接收到的字节（在中断中调用） |

## 接收协议

### 帧格式

```text
[0xA5] [ID] [DATA...] [0x5A]
```

- `0xA5`：帧头
- `ID`：数据类型（用户自定义，1-255）
- `DATA`：ASCII 数据（可选）
- `0x5A`：帧尾

### 事件类型

| 类型 | 说明 | id | value | text |
|------|------|-----|-------|------|
| `TJC_EVENT_BUTTON` | 按钮按下 | 按钮编号 | 0 | 空 |
| `TJC_EVENT_DATA` | 数据输入 | 数据类型 | 数值 | 原始文本 |

### 示例

```c
// 用户自定义 ID（在自己的代码中定义）
#define MY_ID_DISTANCE    1
#define MY_ID_NUMBER      2

// 屏幕发送：A5 01 31 35 30 5A（距离 150）
// 回调收到：
//   event.type = TJC_EVENT_DATA
//   event.id = 1 (MY_ID_DISTANCE)
//   event.value = 150
//   event.text = "150"

// 屏幕发送：A5 02 32 5A（编号 2）
// 回调收到：
//   event.type = TJC_EVENT_DATA
//   event.id = 2 (MY_ID_NUMBER)
//   event.value = 2
//   event.text = "2"

// 屏幕发送：A5 01 5A（按钮 1）
// 回调收到：
//   event.type = TJC_EVENT_BUTTON
//   event.id = 1
```

## 移植到其他平台

### 1. 创建新的 port 文件

实现以下函数：

```c
uint8_t TJC_Port_Init(uint32_t baudrate);
void TJC_Port_SendByte(uint8_t byte);
void TJC_Port_SendBuffer(const uint8_t *buf, uint16_t len);
uint32_t TJC_Port_GetTick(void);
void TJC_Port_Lock(void);
void TJC_Port_Unlock(void);
```

### 2. 在中断中调用 `TJC_HMI_RxByte()`

```c
void USARTx_IRQHandler(void)
{
    if (收到数据) {
        uint8_t byte = 读取数据;
        TJC_HMI_RxByte(byte);
    }
}
```

### 3. 提供 `g_tick` 变量

```c
volatile uint32_t g_tick = 0;

// 在 SysTick 中断中递增
void SysTick_Handler(void)
{
    g_tick++;
}
```

## 配置选项

在 `tjc_hmi_config.h` 中可以修改：

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `TJC_HMI_RX_BUF_SIZE` | 64 | 接收缓冲区大小 |
| `TJC_HMI_TX_BUF_SIZE` | 96 | 发送缓冲区大小 |
| `TJC_HMI_TEXT_MAX_LEN` | 32 | 文本最大长度 |
| `TJC_HMI_ENABLE_FLOAT` | 1 | 启用浮点数显示 |
| `TJC_HMI_ENABLE_PRINTF_FMT` | 1 | 启用格式化发送 |
| `TJC_HMI_ENABLE_RX_PARSE` | 1 | 启用接收解析 |
| `TJC_HMI_FRAME_HEAD` | 0xA5 | 帧头字节 |
| `TJC_HMI_FRAME_TAIL` | 0x5A | 帧尾字节 |

## STM32F103 配置

在 `tjc_hmi_port_stm32f10x.h` 中可以修改：

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `TJC_HMI_USART` | USART1 | 串口外设 |
| `TJC_HMI_GPIO_PORT` | GPIOA | GPIO 端口 |
| `TJC_HMI_TX_PIN` | GPIO_Pin_9 | TX 引脚 |
| `TJC_HMI_RX_PIN` | GPIO_Pin_10 | RX 引脚 |
| `TJC_HMI_NVIC_PREEMPTION` | 1 | 抢占优先级 |
| `TJC_HMI_NVIC_SUB` | 1 | 响应优先级 |

## 屏幕工程建议

### 按钮事件

```text
// 按钮按下，发送 ID
printh A5 01 5A
```

### 输入框事件

```text
// 输入距离
printh A5 01
prints t_distance.txt,0
printh 5A

// 输入编号
printh A5 02
prints t_number.txt,0
printh 5A
```

### 初始化事件

在屏幕初始化事件中添加：

```text
bkcmd=0
```

## 注意事项

1. **帧头帧尾不要用于数据**：0xA5 和 0x5A 是协议保留字节
2. **中断中不要做耗时操作**：回调函数应尽快返回
3. **避免 printf 重定向冲突**：库不依赖 printf，避免和其他串口冲突
4. **ID 建议从 1 开始**：0 保留给特殊用途

## 许可证

MIT License
