/**
  ******************************************************************************
  * @file    tjc_hmi_port_stm32f10x.h
  * @brief   TJC/Nextion 串口屏库 - STM32F103 标准库适配层
  ******************************************************************************
  */

#ifndef __TJC_HMI_PORT_STM32F10X_H
#define __TJC_HMI_PORT_STM32F10X_H

#include "stm32f10x.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===================== 硬件配置（用户可修改） ===================== */

/* 串口外设选择 */
#define TJC_HMI_USART                  USART1
#define TJC_HMI_USART_IRQn             USART1_IRQn
#define TJC_HMI_USART_IRQHandler       USART1_IRQHandler

/* GPIO 配置 */
#define TJC_HMI_GPIO_PORT              GPIOA
#define TJC_HMI_TX_PIN                 GPIO_Pin_9
#define TJC_HMI_RX_PIN                 GPIO_Pin_10

/* 时钟配置 */
#define TJC_HMI_RCC_USART              RCC_APB2Periph_USART1
#define TJC_HMI_RCC_GPIO               RCC_APB2Periph_GPIOA

/* NVIC 优先级 */
#define TJC_HMI_NVIC_PREEMPTION        1
#define TJC_HMI_NVIC_SUB               1

/* ===================== 函数声明 ===================== */

/**
  * @brief  初始化 STM32F103 串口硬件
  * @param  baudrate: 波特率
  * @retval 0=成功
  */
uint8_t TJC_Port_Init(uint32_t baudrate);

/**
  * @brief  发送一个字节
  */
void TJC_Port_SendByte(uint8_t byte);

/**
  * @brief  发送缓冲区数据
  */
void TJC_Port_SendBuffer(const uint8_t *buf, uint16_t len);

/**
  * @brief  获取系统时间戳（毫秒）
  * @note   需要外部提供 g_tick 变量
  */
uint32_t TJC_Port_GetTick(void);

/**
  * @brief  进入临界区（关中断）
  */
void TJC_Port_Lock(void);

/**
  * @brief  退出临界区（开中断）
  */
void TJC_Port_Unlock(void);

#ifdef __cplusplus
}
#endif

#endif /* __TJC_HMI_PORT_STM32F10X_H */
