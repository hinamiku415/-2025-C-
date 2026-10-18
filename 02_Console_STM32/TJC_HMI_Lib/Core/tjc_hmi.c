/**
  ******************************************************************************
  * @file    tjc_hmi.c
  * @brief   TJC/Nextion 串口屏库 - 核心逻辑实现
  *
  *  平台无关的实现，通过 tjc_hmi_port.h 中的函数访问硬件
  ******************************************************************************
  */

#include "tjc_hmi.h"
#include "tjc_hmi_port.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>


/* ═══════════════════════════════════════════════════════════════════════════
 * ██  内部变量
 * ═══════════════════════════════════════════════════════════════════════════ */

/* 事件回调函数 */
static TJC_HMI_Callback_t s_callback = NULL;

/* 接收状态机 */
static TJC_RxState_t s_rx_state = RX_STATE_IDLE;
static uint8_t s_rx_buf[TJC_HMI_RX_BUF_SIZE];
static uint8_t s_rx_pos = 0;

/* 发送缓冲区 */
static char s_tx_buf[TJC_HMI_TX_BUF_SIZE];

/* Nextion 返回包计数（用于跳过） */
static uint8_t s_nextion_ff_count = 0;

/* 调试缓冲区 */
#ifdef TJC_HMI_DEBUG_RX
static uint8_t s_debug_buf[TJC_HMI_DEBUG_BUF_SIZE];
static uint8_t s_debug_idx = 0;
#endif


/* ═══════════════════════════════════════════════════════════════════════════
 * ██  内部函数
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
  * @brief  触发事件回调（调用用户注册的回调函数）
  */
static void _TriggerEvent(const TJC_Event_t *event)
{
    if (s_callback != NULL) {
        s_callback(event);
    }
}

/**
  * @brief  解析通用协议帧（A5 ID DATA 5A）
  */
static void _ParseGenericFrame(void)
{
    if (s_rx_pos < 3) return;  /* 至少需要：帧头 + ID + 帧尾 */

    TJC_Event_t event;
    memset(&event, 0, sizeof(event));

    /* 第二个字节是 ID */
    event.id = s_rx_buf[1];

    /* 检查是否有数据 */
    if (s_rx_pos > 3) {
        /* 有数据，转为数值 */
        uint16_t value = 0;
        for (uint8_t i = 2; i < s_rx_pos - 1; i++) {
            if (s_rx_buf[i] >= '0' && s_rx_buf[i] <= '9') {
                value = value * 10 + (s_rx_buf[i] - '0');
            }
        }
        event.value = value;

        /* 同时保存文本 */
        uint8_t data_len = s_rx_pos - 3;  /* 减去帧头、ID、帧尾 */
        if (data_len >= sizeof(event.text)) {
            data_len = sizeof(event.text) - 1;
        }
        memcpy(event.text, &s_rx_buf[2], data_len);
        event.text[data_len] = '\0';

        /* 有数据的帧 */
        event.type = TJC_EVENT_DATA;
    } else {
        /* 无数据，是按钮事件 */
        event.type = TJC_EVENT_BUTTON;
    }

    /* 保存原始数据 */
    uint8_t raw_len = s_rx_pos;
    if (raw_len >= sizeof(event.raw)) {
        raw_len = sizeof(event.raw) - 1;
    }
    memcpy(event.raw, s_rx_buf, raw_len);
    event.raw_len = raw_len;

    _TriggerEvent(&event);
}


/* ═══════════════════════════════════════════════════════════════════════════
 * ██  接收处理（在中断中调用）
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
  * @brief  喂入接收到的字节（在串口接收中断中调用）
  * @param  byte: 从串口接收到的字节
  */
void TJC_HMI_RxByte(uint8_t byte)
{
#ifdef TJC_HMI_DEBUG_RX
    /* 记录调试字节 */
    s_debug_buf[s_debug_idx & (TJC_HMI_DEBUG_BUF_SIZE - 1)] = byte;
    s_debug_idx++;
#endif

    /* ---- Nextion 返回包处理 ---- */

    /* 检测到 0x65（触摸事件包头），进入跳过模式 */
    if (byte == TJC_HMI_NEXTION_TOUCH_HEAD && s_rx_state == RX_STATE_IDLE) {
        s_rx_state = RX_STATE_SKIP_NEXTION;
        s_nextion_ff_count = 0;
        return;
    }

    /* 跳过 Nextion 返回包，直到收到 3 个连续 FF */
    if (s_rx_state == RX_STATE_SKIP_NEXTION) {
        if (byte == 0xFF) {
            s_nextion_ff_count++;
            if (s_nextion_ff_count >= 3) {
                s_rx_state = RX_STATE_IDLE;
                s_nextion_ff_count = 0;
            }
        } else {
            s_nextion_ff_count = 0;
        }
        return;
    }

    /* 忽略 0xFF（Nextion 结束符，不属于用户数据） */
    if (byte == 0xFF) return;

    /* ---- 通用协议解析（A5 ID DATA 5A） ---- */

    if (byte == TJC_HMI_FRAME_HEAD) {
        /* 收到帧头，开始新帧 */
        s_rx_state = RX_STATE_DATA;
        s_rx_pos = 0;
        s_rx_buf[s_rx_pos++] = byte;
        return;
    }

    if (s_rx_state == RX_STATE_DATA) {
        if (byte == TJC_HMI_FRAME_TAIL) {
            /* 收到帧尾，解析帧 */
            s_rx_buf[s_rx_pos++] = byte;
            _ParseGenericFrame();
            s_rx_state = RX_STATE_IDLE;
            s_rx_pos = 0;
        } else if (s_rx_pos < TJC_HMI_RX_BUF_SIZE - 1) {
            /* 收到数据，存入缓冲区 */
            s_rx_buf[s_rx_pos++] = byte;
        } else {
            /* 缓冲区溢出，丢弃 */
            s_rx_state = RX_STATE_IDLE;
            s_rx_pos = 0;
        }
        return;
    }
}


/* ═══════════════════════════════════════════════════════════════════════════
 * ██  初始化接口
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
  * @brief  初始化串口屏库
  * @param  baudrate: 波特率（如 9600、115200）
  */
void TJC_HMI_Init(uint32_t baudrate)
{
    /* 初始化硬件 */
    TJC_Port_Init(baudrate);

    /* 重置状态机 */
    s_rx_state = RX_STATE_IDLE;
    s_rx_pos = 0;
    s_callback = NULL;

    /* 关闭 Nextion 返回包 */
    TJC_HMI_SetBkcmd(0);
}

/**
  * @brief  注册事件回调函数
  * @param  callback: 回调函数指针
  */
void TJC_HMI_RegisterCallback(TJC_HMI_Callback_t callback)
{
    s_callback = callback;
}


/* ═══════════════════════════════════════════════════════════════════════════
 * ██  发送接口
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
  * @brief  发送指令结束符（FF FF FF）
  */
void TJC_HMI_SendEnd(void)
{
    TJC_Port_SendByte(TJC_HMI_CMD_END_0);
    TJC_Port_SendByte(TJC_HMI_CMD_END_1);
    TJC_Port_SendByte(TJC_HMI_CMD_END_2);
}

/**
  * @brief  发送原始指令（自动追加 FF FF FF 结束符）
  * @param  cmd: 指令字符串，如 "t0.txt=\"hello\""
  */
void TJC_HMI_SendCmd(const char *cmd)
{
    if (cmd == NULL) return;

    uint16_t len = strlen(cmd);
    TJC_Port_SendBuffer((const uint8_t *)cmd, len);
    TJC_HMI_SendEnd();
}

/**
  * @brief  发送原始数据（不追加结束符）
  * @param  data: 数据缓冲区
  * @param  len: 数据长度
  */
void TJC_HMI_SendRaw(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0) return;
    TJC_Port_SendBuffer(data, len);
}


/* ═══════════════════════════════════════════════════════════════════════════
 * ██  控件操作接口
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
  * @brief  设置文本控件内容
  * @param  obj: 控件名，如 "t0"、"D"
  * @param  text: 文本内容
  * @example TJC_HMI_SetText("D", "120.5cm")
  *          生成: D.txt="120.5cm" FF FF FF
  */
void TJC_HMI_SetText(const char *obj, const char *text)
{
    if (obj == NULL || text == NULL) return;

    snprintf(s_tx_buf, sizeof(s_tx_buf), "%s.txt=\"%s\"", obj, text);
    TJC_HMI_SendCmd(s_tx_buf);
}

/**
  * @brief  设置数字控件值
  * @param  obj: 控件名，如 "n0"
  * @param  num: 数值
  * @example TJC_HMI_SetNumber("n0", 123)
  *          生成: n0.val=123 FF FF FF
  */
void TJC_HMI_SetNumber(const char *obj, int32_t num)
{
    if (obj == NULL) return;

    snprintf(s_tx_buf, sizeof(s_tx_buf), "%s.val=%ld", obj, (long)num);
    TJC_HMI_SendCmd(s_tx_buf);
}

/**
  * @brief  设置浮点数（转为文本显示）
  * @param  obj: 控件名
  * @param  value: 浮点数值
  * @param  decimals: 小数位数
  * @example TJC_HMI_SetFloatText("D", 120.5f, 1)
  *          生成: D.txt="120.5" FF FF FF
  */
void TJC_HMI_SetFloatText(const char *obj, float value, uint8_t decimals)
{
    if (obj == NULL) return;

    /* 根据小数位数生成格式字符串 */
    char fmt[16];
    snprintf(fmt, sizeof(fmt), "%%.%uf", decimals);

    char num_str[16];
    snprintf(num_str, sizeof(num_str), fmt, value);

    TJC_HMI_SetText(obj, num_str);
}

/**
  * @brief  切换页面（按名称）
  * @param  page: 页面名称
  * @example TJC_HMI_Page("main")
  *          生成: page main FF FF FF
  */
void TJC_HMI_Page(const char *page)
{
    if (page == NULL) return;

    snprintf(s_tx_buf, sizeof(s_tx_buf), "page %s", page);
    TJC_HMI_SendCmd(s_tx_buf);
}

/**
  * @brief  切换页面（按 ID）
  * @param  page_id: 页面 ID
  * @example TJC_HMI_PageId(1)
  *          生成: page 1 FF FF FF
  */
void TJC_HMI_PageId(uint8_t page_id)
{
    snprintf(s_tx_buf, sizeof(s_tx_buf), "page %u", page_id);
    TJC_HMI_SendCmd(s_tx_buf);
}

/**
  * @brief  设置控件数值属性
  * @param  obj: 控件名
  * @param  attr: 属性名，如 "val"、"pco"
  * @param  value: 数值
  * @example TJC_HMI_SetValue("j0", "val", 50)
  *          生成: j0.val=50 FF FF FF
  */
void TJC_HMI_SetValue(const char *obj, const char *attr, int32_t value)
{
    if (obj == NULL || attr == NULL) return;

    snprintf(s_tx_buf, sizeof(s_tx_buf), "%s.%s=%ld", obj, attr, (long)value);
    TJC_HMI_SendCmd(s_tx_buf);
}

/**
  * @brief  设置控件字符串属性
  * @param  obj: 控件名
  * @param  attr: 属性名，如 "txt"
  * @param  value: 字符串值
  * @example TJC_HMI_SetString("t0", "txt", "OK")
  *          生成: t0.txt="OK" FF FF FF
  */
void TJC_HMI_SetString(const char *obj, const char *attr, const char *value)
{
    if (obj == NULL || attr == NULL || value == NULL) return;

    snprintf(s_tx_buf, sizeof(s_tx_buf), "%s.%s=\"%s\"", obj, attr, value);
    TJC_HMI_SendCmd(s_tx_buf);
}

/**
  * @brief  显示控件
  * @param  obj: 控件名
  * @example TJC_HMI_Show("t0")
  *          生成: vis t0,1
  */
void TJC_HMI_Show(const char *obj)
{
    if (obj == NULL) return;

    snprintf(s_tx_buf, sizeof(s_tx_buf), "vis %s,1", obj);
    TJC_HMI_SendCmd(s_tx_buf);
}

/**
  * @brief  隐藏控件
  * @param  obj: 控件名
  * @example TJC_HMI_Hide("t0")
  *          生成: vis t0,0
  */
void TJC_HMI_Hide(const char *obj)
{
    if (obj == NULL) return;

    snprintf(s_tx_buf, sizeof(s_tx_buf), "vis %s,0", obj);
    TJC_HMI_SendCmd(s_tx_buf);
}

/**
  * @brief  清空文本控件
  * @param  obj: 控件名
  */
void TJC_HMI_ClearText(const char *obj)
{
    TJC_HMI_SetText(obj, "");
}


/* ═══════════════════════════════════════════════════════════════════════════
 * ██  系统指令接口
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
  * @brief  设置指令返回模式
  * @param  mode: 0=不返回, 1=成功时返回, 2=失败时返回, 3=总是返回
  * @example TJC_HMI_SetBkcmd(0)
  *          生成: bkcmd=0 FF FF FF
  */
void TJC_HMI_SetBkcmd(uint8_t mode)
{
    snprintf(s_tx_buf, sizeof(s_tx_buf), "bkcmd=%u", mode);
    TJC_HMI_SendCmd(s_tx_buf);
}


/* ═══════════════════════════════════════════════════════════════════════════
 * ██  调试接口
 * ═══════════════════════════════════════════════════════════════════════════ */

#ifdef TJC_HMI_DEBUG_RX

/**
  * @brief  获取最近接收的字节（调试用）
  * @param  out: 输出缓冲区
  * @param  len: 要获取的字节数
  */
void TJC_HMI_GetDebugBytes(uint8_t *out, uint8_t len)
{
    if (out == NULL || len == 0) return;

    for (uint8_t i = 0; i < len; i++) {
        out[i] = s_debug_buf[(s_debug_idx - len + i) & (TJC_HMI_DEBUG_BUF_SIZE - 1)];
    }
}

#endif /* TJC_HMI_DEBUG_RX */
