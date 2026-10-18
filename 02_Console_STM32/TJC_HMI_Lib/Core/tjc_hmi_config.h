/**
  ******************************************************************************
  * @file    tjc_hmi_config.h
  * @brief   TJC/Nextion 串口屏库 - 用户配置文件
  *
  *  用户根据项目需求修改此文件中的宏定义
  ******************************************************************************
  */

#ifndef __TJC_HMI_CONFIG_H
#define __TJC_HMI_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ===================== 缓冲区大小 ===================== */

/* 接收缓冲区大小（字节） */
#define TJC_HMI_RX_BUF_SIZE            64

/* 发送缓冲区大小（字节） */
#define TJC_HMI_TX_BUF_SIZE            96

/* 文本控件内容最大长度 */
#define TJC_HMI_TEXT_MAX_LEN            32

/* ===================== 功能开关 ===================== */

/* 启用浮点数显示（转为文本） */
#define TJC_HMI_ENABLE_FLOAT            1

/* 启用格式化发送（需要 vsnprintf） */
#define TJC_HMI_ENABLE_PRINTF_FMT       1

/* 启用接收解析（事件回调） */
#define TJC_HMI_ENABLE_RX_PARSE         1

/* ===================== 协议配置 ===================== */

/* 帧头字节（通用协议） */
#define TJC_HMI_FRAME_HEAD              0xA5

/* 帧尾字节（通用协议） */
#define TJC_HMI_FRAME_TAIL              0x5A

/* Nextion 指令结束符 */
#define TJC_HMI_CMD_END_0               0xFF
#define TJC_HMI_CMD_END_1               0xFF
#define TJC_HMI_CMD_END_2               0xFF

/* Nextion 触摸事件包头 */
#define TJC_HMI_NEXTION_TOUCH_HEAD      0x65

/* ===================== 超时配置 ===================== */

/* 帧接收超时（ms），超过此时间未收到帧尾则丢弃 */
#define TJC_HMI_RX_TIMEOUT_MS           100

/* ===================== 调试配置 ===================== */

/* 启用调试模式（记录最近接收字节） */
/* #define TJC_HMI_DEBUG_RX */

/* 调试缓冲区大小 */
#ifdef TJC_HMI_DEBUG_RX
#define TJC_HMI_DEBUG_BUF_SIZE          16
#endif

#ifdef __cplusplus
}
#endif

#endif /* __TJC_HMI_CONFIG_H */
