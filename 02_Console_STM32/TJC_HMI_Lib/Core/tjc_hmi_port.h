/**
  ******************************************************************************
  * @file    tjc_hmi_port.h
  * @brief   TJC/Nextion 串口屏库 - 平台适配接口
  *
  *  用户需要在具体平台的 port 文件中实现这些函数
  *  核心库通过这些函数访问硬件，不直接操作寄存器
  ******************************************************************************
  */

#ifndef __TJC_HMI_PORT_H
#define __TJC_HMI_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===================== 平台适配函数声明 ===================== */

/**
  * @brief  初始化串口硬件
  * @param  baudrate: 波特率（如 9600、115200）
  * @retval 0=成功，非0=失败
  */
uint8_t TJC_Port_Init(uint32_t baudrate);

/**
  * @brief  发送一个字节
  * @param  byte: 要发送的字节
  * @note   阻塞发送，等待发送完成
  */
void TJC_Port_SendByte(uint8_t byte);

/**
  * @brief  发送缓冲区数据
  * @param  buf: 数据缓冲区
  * @param  len: 数据长度
  * @note   阻塞发送，等待发送完成
  */
void TJC_Port_SendBuffer(const uint8_t *buf, uint16_t len);

/**
  * @brief  获取系统时间戳（毫秒）
  * @retval 当前时间戳
  * @note   用于接收超时判断，如果不需要超时功能可返回 0
  */
uint32_t TJC_Port_GetTick(void);

/**
  * @brief  进入临界区（关中断）
  * @note   用于保护共享资源，如果不需要可为空函数
  */
void TJC_Port_Lock(void);

/**
  * @brief  退出临界区（开中断）
  * @note   用于保护共享资源，如果不需要可为空函数
  */
void TJC_Port_Unlock(void);

#ifdef __cplusplus
}
#endif

#endif /* __TJC_HMI_PORT_H */
