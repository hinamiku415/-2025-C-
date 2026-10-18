/**
  ******************************************************************************
  * @file    I2C.h
  * @brief   软件模拟I2C驱动头文件
  *          SCL: PB6，SDA: PB7，开漏输出
  ******************************************************************************
  */

#ifndef __I2C_SW_H
#define __I2C_SW_H

#include "stm32f10x.h"

void    I2C_SW_Init(void);          // 初始化GPIO
void    I2C_SW_Start(void);         // 发送起始信号
void    I2C_SW_Stop(void);          // 发送停止信号
uint8_t I2C_SW_WaitAck(void);       // 等待从机ACK，返回0成功/1超时
void    I2C_SW_SendAck(void);       // 主机发送ACK（读多字节中间用）
void    I2C_SW_SendNAck(void);      // 主机发送NACK（读最后一字节后用）
void    I2C_SW_SendByte(uint8_t byte);   // 发送一个字节
uint8_t I2C_SW_ReceiveByte(void);        // 接收一个字节

#endif /* __I2C_SW_H */
