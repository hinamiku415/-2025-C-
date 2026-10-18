# TJC_HMI_Lib 回调函数机制详解

## 什么是回调函数

回调函数就是**你写的处理函数，库在特定时机自动调用它**。

类比理解：
- 你把电话号码留给快递柜 → `RegisterCallback(MyHandler)`
- 快递柜有快递时打电话给你 → `_TriggerEvent(event)`
- 你接电话去取快递 → `MyHandler()` 被调用

## 两个关键函数

| 函数 | 谁调用 | 作用 |
|------|--------|------|
| `RegisterCallback(MyHandler)` | 你 | 告诉库"我的函数在这" |
| `_TriggerEvent(event)` | 库内部 | 有数据时通知你，调用你的函数 |

## 执行流程

```
屏幕发数据 → 中断读字节 → 库解析帧 → 生成事件 → _TriggerEvent(&event) → MyHandler(&event) → 你的代码
```

## 事件是怎么传给回调的

**核心逻辑：** 库解析完一帧数据后，生成一个事件（`TJC_Event_t`），这个事件作为参数传给你的回调函数。

```c
// 1. 库内部解析完后，生成事件
static void _ParseGenericFrame(void)
{
    TJC_Event_t event;
    event.id = 1;           // 从帧里解析出 ID
    event.value = 150;      // 从帧里解析出数据
    event.type = TJC_EVENT_DATA;
    
    _TriggerEvent(&event);  // 把事件传给回调
}

// 2. _TriggerEvent 调用你的函数，事件作为参数
static void _TriggerEvent(const TJC_Event_t *event)
{
    s_callback(event);  // 你的函数收到事件
}

// 3. 你的回调函数收到事件，处理业务
void MyHandler(const TJC_Event_t *event)
{
    // event->id = 1, event->value = 150
    if (event->id == 1) {
        target_D = event->value;  // 处理业务
    }
}
```

**一句话：** 库解析完 → 生成事件 → 事件作为参数 → 传给你的回调函数 → 你处理业务。

## 代码示例

```c
// 第1步：你写回调函数
void MyHandler(const TJC_Event_t *event)
{
    target_D = event->value;  // 处理业务
}

// 第2步：注册回调
TJC_HMI_RegisterCallback(MyHandler);

// 第3步：库内部自动调用（你看不见）
// _TriggerEvent(&event) → s_callback(event) → MyHandler() 执行
```

## 回调函数流程图

```mermaid
flowchart TD
    subgraph USER["用户代码"]
        A1["写 MyHandler()"]
        A2["RegisterCallback(MyHandler)"]
        A3["MyHandler() 被调用"]
        A4["处理业务"]
    end

    subgraph LIB["库代码 (tjc_hmi.c)"]
        B1["保存函数指针 s_callback"]
        B2["RxByte() 接收字节"]
        B3["解析帧"]
        B4["_TriggerEvent()"]
        B5["s_callback(event)"]
    end

    subgraph HW["硬件"]
        C1["串口屏发数据"]
        C2["USART中断"]
    end

    %% 注册流程
    A1 --> A2
    A2 -->|"传入函数指针"| B1

    %% 触发流程
    C1 --> C2
    C2 -->|"喂字节"| B2
    B2 --> B3
    B3 --> B4
    B4 --> B5
    B5 -->|"调用"| A3
    A3 --> A4

    %% 样式
    style USER fill:#e1f5fe,stroke:#01579b
    style LIB fill:#fff3e0,stroke:#e65100
    style HW fill:#e8f5e8,stroke:#1b5e20
```

## 详细执行流程

```mermaid
sequenceDiagram
    participant User as 用户代码
    participant Lib as 库代码
    participant ISR as 中断
    participant Screen as 串口屏

    Note over User,Screen: 第1步：注册回调
    User->>Lib: RegisterCallback(MyHandler)
    Lib->>Lib: s_callback = MyHandler

    Note over User,Screen: 第2步：屏幕发送数据
    Screen->>ISR: A5 01 31 35 30 5A
    ISR->>Lib: RxByte(0xA5)
    Lib->>Lib: 状态机：帧头
    ISR->>Lib: RxByte(0x01)
    Lib->>Lib: 状态机：ID
    ISR->>Lib: RxByte(0x31)
    Lib->>Lib: 状态机：数据
    ISR->>Lib: RxByte(0x5A)
    Lib->>Lib: 状态机：帧尾

    Note over User,Screen: 第3步：触发回调
    Lib->>Lib: _TriggerEvent(&event)
    Lib->>User: s_callback(event)
    User->>User: MyHandler() 执行
    User->>User: target_D = 150
```

## 代码对照表

```mermaid
flowchart LR
    subgraph STEP1["第1步：你写函数"]
        C1["void MyHandler(const TJC_Event_t *event)\n{\n    target_D = event->value;\n}"]
    end

    subgraph STEP2["第2步：注册"]
        C2["TJC_HMI_RegisterCallback(MyHandler);"]
    end

    subgraph STEP3["第3步：库内部"]
        C3["s_callback = MyHandler;"]
    end

    subgraph STEP4["第4步：触发"]
        C4["_TriggerEvent(&event);\ns_callback(event);"]
    end

    subgraph STEP5["第5步：你的代码执行"]
        C5["MyHandler() 被调用\ntarget_D = 150"]
    end

    STEP1 --> STEP2
    STEP2 --> STEP3
    STEP3 --> STEP4
    STEP4 --> STEP5

    style STEP1 fill:#e1f5fe
    style STEP2 fill:#fff3e0
    style STEP3 fill:#fff3e0
    style STEP4 fill:#e8f5e8
    style STEP5 fill:#fce4ec
```

## 总结

| 概念 | 函数 | 谁调用 | 作用 |
|------|------|--------|------|
| 注册回调 | `RegisterCallback()` | 用户 | 告诉库"我的函数在这" |
| 触发回调 | `_TriggerEvent()` | 库内部 | 有数据时通知用户 |
| 回调函数 | `MyHandler()` | 库调用，用户写 | 处理业务逻辑 |

**一句话：** 你写函数，注册它，库有数据时自动调用它。
