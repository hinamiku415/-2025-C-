# 串口屏通用库抽象计划

## 目标

将当前项目中的 TJC/Nextion 串口屏控制逻辑抽象为一个方便复用的嵌入式串口屏库，使其他单片机项目能够快速完成：

- 串口初始化
- 串口屏指令发送
- 文本、数字、浮点数显示
- 页面切换
- 控件属性设置
- 按钮/输入框事件接收
- 自定义协议解析
- 不依赖具体业务状态机
- 方便移植到 STM32 标准库、STM32 HAL、GD32、CH32、51、ESP32 等平台

库的核心思想是：

> 串口屏库只负责"屏幕通信和控件操作"，不直接修改业务变量，也不直接依赖某个具体 MCU 的寄存器或外设库。

---

## 当前项目可参考内容

当前项目中串口屏相关逻辑主要分布在：

- `Hardware/Serial.c`
- `Hardware/Serial.h`
- `Hardware/HMI.c`
- `Hardware/HMI.h`
- `User/main.c`
- `TJC_HMI_踩坑记录.md`

当前实现包含以下能力：

- USART1 初始化，波特率 `9600`
- `printf` 重定向到 USART1
- 发送 Nextion/TJC 格式指令，结尾追加 `0xFF 0xFF 0xFF`
- 设置文本控件内容
- 接收串口屏发来的单字节命令
- 接收带包头包尾的数据帧
- 解析距离参数 `target_D`
- 解析编号参数 `target_N`
- 忽略 TJC/Nextion 的部分返回包干扰

这些逻辑可以作为抽库参考，但不能原样作为通用库，因为当前 `HMI.c` 直接耦合了：

- `currentState`
- `target_D`
- `target_N`
- `g_param_updated`
- `g_dist_d`
- `g_width_x`
- `main.h`

通用库中应移除这些业务依赖。

---

## 设计原则

### 1. 分层设计

库分为三层：

- 平台适配层：负责具体 MCU 的串口初始化和字节收发。
- 屏幕协议层：负责 TJC/Nextion 指令封装、接收解析、事件生成。
- 用户应用层：用户自己处理按钮、输入框、状态机等业务逻辑。

### 2. 不强制使用 `printf`

库内部不依赖 `printf` 重定向，避免和其他串口冲突。

可以提供格式化接口，但底层发送必须走统一的 `send_byte` 或 `send_buffer` 回调。

### 3. 不在库内处理业务变量

库不直接修改 `target_D`、`currentState` 等变量。

库只产生事件，例如：

- 按钮按下
- 收到复位命令
- 收到数字输入
- 收到文本输入
- 收到自定义帧

业务层通过回调函数处理这些事件。

### 4. 支持不同平台

核心库文件不包含：

- `stm32f10x.h`
- `stm32f1xx_hal.h`
- `main.h`
- 任何具体芯片头文件

具体平台代码放到独立 port 文件中。

### 5. 支持阻塞发送，预留非阻塞发送

第一版以阻塞发送为主，便于比赛和小项目使用。

后续可以扩展 DMA、环形缓冲区、中断发送。

---

## 建议目录结构

```text
TJC_HMI_Lib/
├── Core/
│   ├── tjc_hmi.c
│   ├── tjc_hmi.h
│   ├── tjc_hmi_config.h
│   └── tjc_hmi_types.h
├── Port/
│   ├── stm32f10x_std/
│   │   ├── tjc_hmi_port.c
│   │   └── tjc_hmi_port.h
│   ├── stm32_hal/
│   │   ├── tjc_hmi_port.c
│   │   └── tjc_hmi_port.h
│   └── generic/
│       ├── tjc_hmi_port.c
│       └── tjc_hmi_port.h
├── Examples/
│   ├── stm32f103_std_demo/
│   ├── stm32_hal_demo/
│   └── generic_demo/
└── README.md
```

如果先在当前项目内落地，可以简化为：

```text
Lib/TJC_HMI/
├── tjc_hmi.c
├── tjc_hmi.h
├── tjc_hmi_config.h
├── tjc_hmi_types.h
└── port/
    ├── tjc_hmi_port_stm32f10x.c
    └── tjc_hmi_port_stm32f10x.h
```

---

## 核心模块职责

### 1. `tjc_hmi.h`

对外主接口。

负责声明：

- 初始化接口
- 发送指令接口
- 控件操作接口
- 接收字节处理接口
- 回调注册接口
- 页面切换接口
- 常用属性设置接口

### 2. `tjc_hmi.c`

平台无关的核心实现。

负责：

- 生成 TJC/Nextion 指令
- 追加 `0xFF 0xFF 0xFF`
- 格式化文本、数字、浮点数
- 解析收到的字节
- 维护接收状态机
- 触发用户回调

不能包含任何 MCU 相关头文件。

### 3. `tjc_hmi_types.h`

定义通用数据类型。

例如：

```c
typedef enum {
    TJC_EVENT_NONE = 0,
    TJC_EVENT_BUTTON,
    TJC_EVENT_NUMBER,
    TJC_EVENT_TEXT,
    TJC_EVENT_CUSTOM_FRAME,
    TJC_EVENT_RESET,
    TJC_EVENT_ERROR
} TJC_EventType_t;

typedef struct {
    TJC_EventType_t type;
    uint8_t id;
    uint16_t value;
    char text[32];
    uint8_t raw[32];
    uint8_t raw_len;
} TJC_Event_t;
```

### 4. `tjc_hmi_config.h`

用户配置文件。

建议包含：

```c
#define TJC_HMI_RX_BUF_SIZE        64
#define TJC_HMI_TEXT_BUF_SIZE      64
#define TJC_HMI_ENABLE_FLOAT       1
#define TJC_HMI_ENABLE_PRINTF_FMT  1
#define TJC_HMI_ENABLE_RX_PARSE    1
#define TJC_HMI_MAX_CMD_LEN        96
```

### 5. `tjc_hmi_port.h`

平台适配接口声明。

核心库通过这些函数访问硬件：

```c
void TJC_Port_Init(uint32_t baudrate);
void TJC_Port_SendByte(uint8_t byte);
void TJC_Port_SendBuffer(const uint8_t *buf, uint16_t len);
uint32_t TJC_Port_GetTick(void);
void TJC_Port_Lock(void);
void TJC_Port_Unlock(void);
```

### 6. `tjc_hmi_port_stm32f10x.c`

当前项目可先实现 STM32F103 标准库版本。

负责：

- 开启 USART1 时钟
- 配置 GPIO PA9/PA10
- 配置 USART1
- 配置 NVIC
- 实现 `USART1_IRQHandler`
- 在中断中调用 `TJC_HMI_RxByte(byte)`

---

## 第一版 API 设计

### 初始化

```c
void TJC_HMI_Init(uint32_t baudrate);
void TJC_HMI_RegisterCallback(TJC_HMI_Callback_t callback);
```

### 原始命令发送

```c
void TJC_HMI_SendCmd(const char *cmd);
void TJC_HMI_SendRaw(const uint8_t *data, uint16_t len);
void TJC_HMI_SendEnd(void);
```

`TJC_HMI_SendCmd()` 自动追加：

```text
0xFF 0xFF 0xFF
```

### 文本控件

```c
void TJC_HMI_SetText(const char *obj, const char *text);
```

示例：

```c
TJC_HMI_SetText("t0", "hello");
TJC_HMI_SetText("D", "120.5cm");
```

生成：

```text
t0.txt="hello" FF FF FF
D.txt="120.5cm" FF FF FF
```

### 数字控件

```c
void TJC_HMI_SetNumber(const char *obj, int32_t num);
```

示例：

```c
TJC_HMI_SetNumber("n0", 123);
```

生成：

```text
n0.val=123 FF FF FF
```

### 浮点显示

TJC/Nextion 本身没有真正的浮点控件，建议转成文本显示：

```c
void TJC_HMI_SetFloatText(const char *obj, float value, uint8_t decimals);
```

示例：

```c
TJC_HMI_SetFloatText("D", 120.5f, 1);
```

生成：

```text
D.txt="120.5" FF FF FF
```

### 页面切换

```c
void TJC_HMI_Page(const char *page);
void TJC_HMI_PageId(uint8_t page_id);
```

示例：

```c
TJC_HMI_Page("main");
TJC_HMI_PageId(1);
```

生成：

```text
page main FF FF FF
page 1 FF FF FF
```

### 控件属性设置

```c
void TJC_HMI_SetValue(const char *obj, const char *attr, int32_t value);
void TJC_HMI_SetString(const char *obj, const char *attr, const char *value);
```

示例：

```c
TJC_HMI_SetValue("j0", "val", 50);
TJC_HMI_SetValue("t0", "pco", 63488);
TJC_HMI_SetString("t0", "txt", "OK");
```

生成：

```text
j0.val=50 FF FF FF
t0.pco=63488 FF FF FF
t0.txt="OK" FF FF FF
```

### 可见性控制

```c
void TJC_HMI_Show(const char *obj);
void TJC_HMI_Hide(const char *obj);
```

生成：

```text
vis obj,1
vis obj,0
```

### 清空控件

```c
void TJC_HMI_ClearText(const char *obj);
```

等价于：

```c
TJC_HMI_SetText(obj, "");
```

### 蜂鸣/提示

如果屏幕工程内有对应变量或控件，可先不做专用 API。

第一版可以只提供通用命令：

```c
TJC_HMI_SendCmd("click b0,1");
```

---

## 接收设计

### 基础接收入口

所有 MCU 平台在串口接收中断中调用：

```c
void TJC_HMI_RxByte(uint8_t byte);
```

示例：

```c
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
        uint8_t byte = USART_ReceiveData(USART1);
        TJC_HMI_RxByte(byte);
        USART_ClearITPendingBit(USART1, USART_IT_RXNE);
    }
}
```

### 事件回调

用户注册回调：

```c
typedef void (*TJC_HMI_Callback_t)(const TJC_Event_t *event);
```

示例：

```c
static void HMI_EventHandler(const TJC_Event_t *event)
{
    switch (event->type) {
        case TJC_EVENT_BUTTON:
            break;

        case TJC_EVENT_NUMBER:
            break;

        case TJC_EVENT_TEXT:
            break;

        default:
            break;
    }
}
```

初始化：

```c
TJC_HMI_Init(9600);
TJC_HMI_RegisterCallback(HMI_EventHandler);
```

---

## 推荐接收协议

为了方便其他单片机使用，库不要强绑定当前项目的 `0xAA/0xBB/0xCC`，但可以内置一个简单通用协议。

### 通用推荐协议

```text
[HEAD] [ID] [DATA...] [END]
```

建议定义：

```text
0xA5        帧头
ID          数据类型或控件编号
DATA        ASCII 数据
0x5A        帧尾
```

示例：

```text
A5 01 31 32 33 5A
```

表示：

```text
ID = 1
DATA = "123"
```

事件：

```c
event.type = TJC_EVENT_NUMBER;
event.id = 1;
event.value = 123;
```

### 兼容当前项目协议

为了当前项目迁移方便，可以在配置中启用兼容模式：

```c
#define TJC_HMI_ENABLE_LEGACY_AA_BB_CC  1
```

当前项目协议：

```text
0xAA 数字... 0xCC  -> 距离输入
0xBB 数字... 0xCC  -> 编号输入
0x00              -> 复位
状态字节           -> 按钮/模式切换
```

解析成事件：

```c
0xAA ... 0xCC -> TJC_EVENT_NUMBER, id = TJC_ID_DISTANCE
0xBB ... 0xCC -> TJC_EVENT_NUMBER, id = TJC_ID_NUMBER
0x00          -> TJC_EVENT_RESET
状态字节       -> TJC_EVENT_BUTTON
```

建议定义：

```c
#define TJC_ID_DISTANCE  1
#define TJC_ID_NUMBER    2
```

---

## Nextion/TJC 返回包处理

TJC/Nextion 默认会发送返回包，例如：

```text
0x01 0xFF 0xFF 0xFF
0x65 page_id component_id event 0xFF 0xFF 0xFF
```

库需要处理两类策略。

### 策略 1：初始化时关闭返回包

初始化后发送：

```c
TJC_HMI_SendCmd("bkcmd=0");
```

提供接口：

```c
void TJC_HMI_SetBkcmd(uint8_t mode);
```

调用：

```c
TJC_HMI_SetBkcmd(0);
```

### 策略 2：接收状态机主动过滤

接收解析器要能识别并丢弃：

- `0xFF 0xFF 0xFF` 结束符
- `0x65 ... 0xFF 0xFF 0xFF` 触摸事件包
- 常见返回状态码

不要让返回包污染用户自定义协议。

---

## 平台适配设计

### STM32F103 标准库适配

第一版优先实现当前项目可用的版本。

默认配置：

```text
USART1
TX: PA9
RX: PA10
Baudrate: 9600
NVIC: USART1_IRQn
```

提供可修改宏：

```c
#define TJC_HMI_USART              USART1
#define TJC_HMI_USART_IRQn         USART1_IRQn
#define TJC_HMI_GPIO_PORT          GPIOA
#define TJC_HMI_TX_PIN             GPIO_Pin_9
#define TJC_HMI_RX_PIN             GPIO_Pin_10
#define TJC_HMI_RCC_USART          RCC_APB2Periph_USART1
#define TJC_HMI_RCC_GPIO           RCC_APB2Periph_GPIOA
```

### STM32 HAL 适配

后续单独实现。

核心逻辑：

```c
HAL_UART_Receive_IT(&huart, &rx_byte, 1);

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    TJC_HMI_RxByte(rx_byte);
    HAL_UART_Receive_IT(huart, &rx_byte, 1);
}
```

### Generic 适配

给其他 MCU 使用。

用户只需要实现：

```c
void TJC_Port_Init(uint32_t baudrate);
void TJC_Port_SendByte(uint8_t byte);
void TJC_Port_SendBuffer(const uint8_t *buf, uint16_t len);
```

接收时用户手动喂入：

```c
TJC_HMI_RxByte(byte);
```

---

## 当前项目迁移方案

### 第一步：保留旧接口，新增库

先不要大范围改业务逻辑。

新增库后，保留兼容 API：

```c
void HMI_SendCmd(char *cmd)
{
    TJC_HMI_SendCmd(cmd);
}

void HMI_SetText(char *obj, char *value)
{
    TJC_HMI_SetText(obj, value);
}
```

这样 `main.c` 和 `Pi_Comm.c` 可以暂时不改。

### 第二步：把 USART1 初始化迁移到 port

旧的：

```c
Serial_Init();
```

改为：

```c
TJC_HMI_Init(9600);
```

或者为了兼容，暂时：

```c
void Serial_Init(void)
{
    TJC_HMI_Init(9600);
}
```

### 第三步：把 `USART1_IRQHandler` 简化

旧中断里直接修改业务变量。

新中断只做：

```c
uint8_t byte = USART_ReceiveData(USART1);
TJC_HMI_RxByte(byte);
```

### 第四步：业务事件回调放到应用层

在当前项目中新建：

```text
Hardware/App_HMI.c
Hardware/App_HMI.h
```

处理当前项目业务：

```c
static void AppHMI_EventHandler(const TJC_Event_t *event)
{
    switch (event->type) {
        case TJC_EVENT_RESET:
            currentState = STATE_IDLE;
            UI_UpdateResult(g_dist_d, g_width_x);
            break;

        case TJC_EVENT_BUTTON:
            currentState = AppHMI_MapButtonToState(event->id);
            break;

        case TJC_EVENT_NUMBER:
            if (event->id == TJC_ID_DISTANCE) {
                target_D = event->value;
                g_param_updated = 1;
            } else if (event->id == TJC_ID_NUMBER) {
                target_N = event->value;
                g_param_updated = 1;
            }
            break;

        default:
            break;
    }
}
```

### 第五步：清理旧文件

确认功能正常后，再逐步清理：

- `Command_Parser()`
- `UART_DataReady`
- `HMI.c` 中直接修改业务变量的逻辑
- `Serial.c` 里旧的 `HMI_send_string`
- `Serial.c` 里旧的 `HMI_send_number`
- `Serial.c` 里旧的 `HMI_send_float`

---

## 建议优先实现的功能清单

### V1 必须实现

- 串口初始化
- 发送原始指令
- 自动追加 `0xFF 0xFF 0xFF`
- 设置文本控件
- 设置数字控件
- 设置浮点文本
- 页面切换
- 控件属性设置
- 串口接收字节入口
- 用户事件回调
- 当前项目协议兼容
- STM32F103 标准库 port

### V1 可选实现

- 显示/隐藏控件
- 清空文本
- 设置颜色
- 设置进度条
- 设置 `bkcmd`
- 调试接收最近字节

### V2 再考虑

- DMA 发送
- 环形发送缓冲区
- 多串口屏实例
- 多页面控件绑定
- 自动重发
- 指令应答确认
- CRC 帧协议
- RTOS 队列事件
- HAL/LL/GD32/CH32/ESP32 port

---

## 示例用法

### 最小使用

```c
#include "tjc_hmi.h"

int main(void)
{
    TJC_HMI_Init(9600);

    TJC_HMI_SetBkcmd(0);
    TJC_HMI_SetText("t0", "Hello");
    TJC_HMI_SetNumber("n0", 123);
    TJC_HMI_SetFloatText("D", 120.5f, 1);

    while (1) {
    }
}
```

### 带事件回调

```c
static void HMI_Callback(const TJC_Event_t *event)
{
    if (event->type == TJC_EVENT_BUTTON) {
        if (event->id == 1) {
            TJC_HMI_SetText("status", "Button 1");
        }
    }

    if (event->type == TJC_EVENT_NUMBER) {
        if (event->id == TJC_ID_DISTANCE) {
            target_D = event->value;
        }
    }
}

int main(void)
{
    TJC_HMI_Init(9600);
    TJC_HMI_RegisterCallback(HMI_Callback);
    TJC_HMI_SetBkcmd(0);

    while (1) {
    }
}
```

---

## 屏幕工程推荐写法

### 按钮发送 ID

推荐按钮事件中写：

```text
printh A5 01 31 5A
```

表示按钮 ID 1。

或者简单单字节模式：

```text
printh 81
```

### 输入距离

推荐：

```text
printh A5 10
prints t_distance.txt,0
printh 5A
```

其中：

```text
0x10 = 距离输入 ID
```

### 输入编号

```text
printh A5 11
prints t_number.txt,0
printh 5A
```

其中：

```text
0x11 = 编号输入 ID
```

### 初始化页面

建议在 TJC/Nextion 页面初始化事件中写：

```text
bkcmd=0
```

或者由 MCU 上电后发送：

```c
TJC_HMI_SetBkcmd(0);
```

---

## 风险点

### 1. 状态字节不要使用 `0x01`

TJC/Nextion 成功返回包可能包含 `0x01`，容易误触发。

建议业务按钮使用 `0x80` 以上的字节，或者使用完整帧协议。

### 2. 不要用 `\r\n` 作为唯一结束符

`\r\n` 是两个字节，容易造成解析竞争或重复触发。

建议使用单字节包尾，例如：

```text
0x5A
```

或当前项目中的：

```text
0xCC
```

### 3. 不要让 ISR 做太重的事

接收中断里不要大量 `sprintf`、`atoi`、屏幕刷新。

第一版如果为了简单可以在 `TJC_HMI_RxByte()` 中做轻量解析，但不要在回调里做耗时操作。

如果后续使用 RTOS，应改为 ISR 投递事件，主循环或任务处理。

### 4. 避免全局 `printf` 绑定冲突

当前项目 `printf` 被重定向到 USART1。

通用库不要依赖这个机制，否则其他项目中 `printf` 可能已经用于调试串口。

---

## 验证计划

### 发送验证

用串口助手监听 MCU TX，调用：

```c
TJC_HMI_SetText("t0", "abc");
```

应看到：

```text
74 30 2E 74 78 74 3D 22 61 62 63 22 FF FF FF
```

即：

```text
t0.txt="abc" FF FF FF
```

### 接收验证

向 MCU RX 发送：

```text
A5 10 31 32 33 5A
```

应触发：

```c
event.type = TJC_EVENT_NUMBER
event.id = 0x10
event.value = 123
```

### 当前项目兼容验证

发送：

```text
AA 31 35 30 CC
```

应触发距离事件：

```c
id = TJC_ID_DISTANCE
value = 150
```

发送：

```text
BB 32 CC
```

应触发编号事件：

```c
id = TJC_ID_NUMBER
value = 2
```

发送：

```text
00
```

应触发复位事件。

### 返回包干扰验证

发送：

```text
01 FF FF FF
```

如果启用过滤策略，不应误触发按钮事件。

发送：

```text
65 00 01 01 FF FF FF
```

不应污染下一帧解析。

---

## 最终交付物

完成后应包含：

- `tjc_hmi.c`
- `tjc_hmi.h`
- `tjc_hmi_types.h`
- `tjc_hmi_config.h`
- `tjc_hmi_port.h`
- `tjc_hmi_port_stm32f10x.c`
- `tjc_hmi_port_stm32f10x.h`
- 当前项目的迁移示例
- 一个最小 demo
- README 使用说明
- 屏幕工程事件写法说明

---

## 推荐执行顺序

1. 先实现核心发送 API。
2. 再实现 STM32F103 标准库 port。
3. 用当前项目替换 `HMI_SetText()`、`HMI_SendCmd()`。
4. 实现接收状态机和事件回调。
5. 把当前项目的状态切换、距离输入、编号输入迁移到回调。
6. 清理旧 `HMI.c` 和 `Serial.c` 中重复接口。
7. 补充 README 和示例。
8. 最后再考虑 HAL、DMA、多实例等扩展。
