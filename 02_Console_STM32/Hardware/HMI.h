/**
  ******************************************************************************
  * @file    HMI.h
  * @brief   HMI串口屏驱动头文件
  *
  *  HMI屏通过USART1（9600bps）与STM32通信：
  *  - 接收：单字节状态切换指令 + 文本参数指令（D=xxx / N=x）
  *  - 发送：Nextion格式指令（obj.txt="value" + 0xFF 0xFF 0xFF）
  ******************************************************************************
  */

#ifndef __HMI_H
#define __HMI_H

#include <stdio.h>
#include "stm32f10x.h"

/* 调试开关：开启后OLED第4行显示最近4个原始接收字节（十六进制）  */
/* 确认字节序列正确后注释掉此行                                   */
/* #define HMI_DEBUG_RX */

/* 文本帧接收完成标志（外部可读，由 Command_Parser 清零） */
extern volatile uint8_t UART_DataReady;

/* 主循环中调用：解析HMI发来的文本参数指令 */
void Command_Parser(void);

/* 显示更新接口 */
void UI_UpdatePower(int ma, int mw);
void UI_UpdateResult(float d, float x);
void UI_UpdateResultWithNum(float d, float x, uint8_t num);

/* HMI指令发送接口 */
void HMI_SendCmd(char *cmd);
void HMI_SetText(char *obj, char *value);

/* 调试接口：获取最近收到的4个原始字节（HMI_DEBUG_RX开启时有效） */
void HMI_GetDebugBytes(uint8_t out[4]);

#endif /* __HMI_H */
