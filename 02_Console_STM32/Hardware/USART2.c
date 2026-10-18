/**
  ******************************************************************************
  * @file    USART2.c
  * @brief   树莓派串口通信驱动
  *
  *  当前使用 USART2：PA2(TX) / PA3(RX)，波特率115200
  *  （NRF24L01 软件SPI 引脚已迁移，PA2/PA3 不再冲突）
  *
  *  备用配置（USART3，PB10/PB11）已注释保留，PCB走线修复后可切回。
  *
  *  环形行缓冲区（4槽）：
  *    中断收到\n后把当前行存入下一个槽，不等主循环处理
  *    主循环通过 GetRxFlag/GetRxBuffer/ClearRxBuffer 逐行取出
  ******************************************************************************
  */

#include "stm32f10x.h"
#include "USART2.h"

#define USART2_RX_BUF_SIZE  128
#define BUF_COUNT           4

/* 环形行缓冲区 */
static uint8_t  s_buf[BUF_COUNT][USART2_RX_BUF_SIZE];
static uint16_t s_wr_len = 0;
static uint8_t  s_wr_idx = 0;
static volatile uint8_t s_rd_idx = 0;
static volatile uint8_t s_count  = 0;

/* 调试用 */
volatile uint16_t g_usart3_rx_count = 0;
volatile uint8_t  g_max_buf_count   = 0;

/* ===================== 初始化 ===================== */

void USART2_Init(void)
{
    /* ── 当前：USART2，PA2(TX) / PA3(RX) ── */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure;

    /* PA2: TX - 复用推挽输出 */
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* PA3: RX - 浮空输入 */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_3;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART_InitTypeDef USART_InitStructure;
    USART_InitStructure.USART_BaudRate            = 115200;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_Init(USART2, &USART_InitStructure);

    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);

    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel                   = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 2;
    NVIC_Init(&NVIC_InitStructure);

    USART_Cmd(USART2, ENABLE);

    /* ── 备用：USART3，PB10(TX) / PB11(RX)（PCB走线修复后切回）──
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_11;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    USART_Init(USART3, &USART_InitStructure);
    USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel                   = USART3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 2;
    NVIC_Init(&NVIC_InitStructure);

    USART_Cmd(USART3, ENABLE);
    ── */
}

/* ===================== 发送 ===================== */

void USART2_SendByte(uint8_t byte)
{
    USART_SendData(USART2, byte);
    while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
}

void USART2_SendArray(uint8_t *array, uint16_t length)
{
    for (uint16_t i = 0; i < length; i++) USART2_SendByte(array[i]);
}

void USART2_SendString(char *str)
{
    while (*str) USART2_SendByte(*str++);
}

/* ===================== 接收（环形缓冲区） ===================== */

uint8_t USART2_GetRxFlag(void)
{
    return (s_count > 0) ? 1 : 0;
}

uint8_t* USART2_GetRxBuffer(void)
{
    return s_buf[s_rd_idx];
}

void USART2_ClearRxBuffer(void)
{
    if (s_count > 0) {
        s_buf[s_rd_idx][0] = '\0';
        s_rd_idx = (s_rd_idx + 1) % BUF_COUNT;
        s_count--;
    }
}

/* ===================== 中断 ===================== */

void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET) {
        uint8_t res = USART_ReceiveData(USART2);
        g_usart3_rx_count++;

        if (res == '\n' || res == '\r') {
            if (s_wr_len > 0) {
                s_buf[s_wr_idx][s_wr_len] = '\0';
                uint8_t next = (s_wr_idx + 1) % BUF_COUNT;
                if (s_count < BUF_COUNT) {
                    s_count++;
                } else {
                    s_rd_idx = (s_rd_idx + 1) % BUF_COUNT;
                }
                s_wr_idx = next;
                s_wr_len = 0;
                if (s_count > g_max_buf_count) g_max_buf_count = s_count;
            }
        } else {
            if (s_wr_len < USART2_RX_BUF_SIZE - 1) {
                s_buf[s_wr_idx][s_wr_len++] = res;
            }
        }

        USART_ClearITPendingBit(USART2, USART_IT_RXNE);
    }
}

/* ── 备用：USART3 中断（PB10/PB11，PCB走线修复后切回）──
void USART3_IRQHandler(void)
{
    if (USART_GetITStatus(USART3, USART_IT_RXNE) != RESET) {
        uint8_t res = USART_ReceiveData(USART3);
        g_usart3_rx_count++;

        if (res == '\n' || res == '\r') {
            if (s_wr_len > 0) {
                s_buf[s_wr_idx][s_wr_len] = '\0';
                uint8_t next = (s_wr_idx + 1) % BUF_COUNT;
                if (s_count < BUF_COUNT) {
                    s_count++;
                } else {
                    s_rd_idx = (s_rd_idx + 1) % BUF_COUNT;
                }
                s_wr_idx = next;
                s_wr_len = 0;
                if (s_count > g_max_buf_count) g_max_buf_count = s_count;
            }
        } else {
            if (s_wr_len < USART2_RX_BUF_SIZE - 1) {
                s_buf[s_wr_idx][s_wr_len++] = res;
            }
        }

        USART_ClearITPendingBit(USART3, USART_IT_RXNE);
    }
}
── */
