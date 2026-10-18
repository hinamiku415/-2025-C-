/**
  ******************************************************************************
  * @file    tjc_hmi_types.h
  * @brief   TJC/Nextion 串口屏库 - 数据类型定义
  ******************************************************************************
  */

#ifndef __TJC_HMI_TYPES_H
#define __TJC_HMI_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===================== 事件类型 ===================== */

typedef enum {
    TJC_EVENT_NONE = 0,         /* 无事件 */
    TJC_EVENT_BUTTON,           /* 按钮按下（无数据，只有 ID） */
    TJC_EVENT_DATA,             /* 数据输入（有 ID + 数据） */
    TJC_EVENT_ERROR             /* 错误 */
} TJC_EventType_t;

/* ===================== 事件结构体 ===================== */

typedef struct {
    TJC_EventType_t type;       /* 事件类型 */
    uint8_t id;                 /* 事件 ID（用户自定义含义） */
    uint16_t value;             /* 数值（DATA 事件时有效） */
    char text[32];              /* 文本（DATA 事件时有效） */
    uint8_t raw[32];            /* 原始数据 */
    uint8_t raw_len;            /* 原始数据长度 */
} TJC_Event_t;

/* ===================== 回调函数类型 ===================== */

typedef void (*TJC_HMI_Callback_t)(const TJC_Event_t *event);

/* ===================== 接收状态机状态 ===================== */

typedef enum {
    RX_STATE_IDLE = 0,          /* 空闲，等待帧头 */
    RX_STATE_DATA,              /* 接收数据中 */
    RX_STATE_SKIP_NEXTION       /* 跳过 Nextion 返回包 */
} TJC_RxState_t;

#ifdef __cplusplus
}
#endif

#endif /* __TJC_HMI_TYPES_H */
