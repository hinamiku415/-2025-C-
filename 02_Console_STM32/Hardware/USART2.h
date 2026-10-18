/**
  ******************************************************************************
  * @file    USART2.h
  * @brief   USART2驱动头文件（与树莓派通信）
  ******************************************************************************
  */

#ifndef __USART2_H
#define __USART2_H

#include "stm32f10x.h"

void    USART2_Init(void);
void    USART2_SendByte(uint8_t byte);
void    USART2_SendArray(uint8_t *array, uint16_t length);
void    USART2_SendString(char *str);
uint8_t USART2_GetRxFlag(void);
uint8_t* USART2_GetRxBuffer(void);
void    USART2_ClearRxBuffer(void);
void    USART2_PollRx(void);   // 轮询接收（调试用）

/* 调试用：USART3收到的字节总数，若一直为0说明中断未触发 */
extern volatile uint16_t g_usart3_rx_count;
extern volatile uint8_t  g_max_buf_count;

#endif /* __USART2_H */
