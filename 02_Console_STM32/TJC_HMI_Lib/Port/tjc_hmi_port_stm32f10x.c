/**
  ******************************************************************************
  * @file    tjc_hmi_port_stm32f10x.c
  * @brief   TJC/Nextion 串口屏库 - STM32F103 标准库适配层实现
  *
  *  默认配置：
  *    USART1, PA9(TX) / PA10(RX), 9600bps
  *
  *  用户可修改 tjc_hmi_port_stm32f10x.h 中的宏定义来更改配置
  ******************************************************************************
  */

#include "stm32f10x.h"
#include "tjc_hmi_port_stm32f10x.h"
#include "tjc_hmi.h"

/* ===================== 外部变量 ===================== */

/* 系统滴答计数器（需要由用户提供，如 SysTick 中断中递增） */
extern volatile uint32_t g_tick;

/* ===================== 初始化 ===================== */

/**
  * @brief  初始化 STM32F103 串口硬件
  */
uint8_t TJC_Port_Init(uint32_t baudrate)
{
    /* 开启时钟 */
    RCC_APB2PeriphClockCmd(TJC_HMI_RCC_USART, ENABLE);
    RCC_APB2PeriphClockCmd(TJC_HMI_RCC_GPIO, ENABLE);

    /* GPIO 配置 */
    GPIO_InitTypeDef GPIO_InitStructure;

    /* TX: 复用推挽输出 */
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Pin   = TJC_HMI_TX_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(TJC_HMI_GPIO_PORT, &GPIO_InitStructure);

    /* RX: 浮空输入 */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_InitStructure.GPIO_Pin  = TJC_HMI_RX_PIN;
    GPIO_Init(TJC_HMI_GPIO_PORT, &GPIO_InitStructure);

    /* USART 配置 */
    USART_InitTypeDef USART_InitStructure;
    USART_InitStructure.USART_BaudRate            = baudrate;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_Init(TJC_HMI_USART, &USART_InitStructure);

    /* 开启接收中断 */
    USART_ITConfig(TJC_HMI_USART, USART_IT_RXNE, ENABLE);

    /* NVIC 配置 */
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel                   = TJC_HMI_USART_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = TJC_HMI_NVIC_PREEMPTION;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = TJC_HMI_NVIC_SUB;
    NVIC_Init(&NVIC_InitStructure);

    /* 使能 USART */
    USART_Cmd(TJC_HMI_USART, ENABLE);

    return 0;
}

/* ===================== 发送 ===================== */

/**
  * @brief  发送一个字节
  */
void TJC_Port_SendByte(uint8_t byte)
{
    USART_SendData(TJC_HMI_USART, byte);
    while (USART_GetFlagStatus(TJC_HMI_USART, USART_FLAG_TXE) == RESET);
}

/**
  * @brief  发送缓冲区数据
  */
void TJC_Port_SendBuffer(const uint8_t *buf, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        TJC_Port_SendByte(buf[i]);
    }
}

/* ===================== 系统 ===================== */

/**
  * @brief  获取系统时间戳（毫秒）
  */
uint32_t TJC_Port_GetTick(void)
{
    return g_tick;
}

/**
  * @brief  进入临界区（关中断）
  */
void TJC_Port_Lock(void)
{
    __disable_irq();
}

/**
  * @brief  退出临界区（开中断）
  */
void TJC_Port_Unlock(void)
{
    __enable_irq();
}

/* ===================== 中断处理 ===================== */

/**
  * @brief  USART1 中断服务函数
  * @note   在 stm32f10x_it.c 中调用，或直接放在此文件
  */
void TJC_HMI_USART_IRQHandler(void)
{
    if (USART_GetITStatus(TJC_HMI_USART, USART_IT_RXNE) != RESET) {
        uint8_t byte = USART_ReceiveData(TJC_HMI_USART);
        TJC_HMI_RxByte(byte);
        USART_ClearITPendingBit(TJC_HMI_USART, USART_IT_RXNE);
    }
}
