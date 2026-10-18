/**
  ******************************************************************************
  * @file    I2C.c
  * @brief   软件模拟I2C驱动（标准库）
  *          SCL: PB6，SDA: PB7，开漏输出 + 外部上拉
  *
  *  时序说明：
  *    起始信号：SCL高电平期间，SDA下降沿
  *    停止信号：SCL高电平期间，SDA上升沿
  *    数据传输：高位先行，SCL上升沿采样
  ******************************************************************************
  */

#include "I2C.h"

/* ===================== 引脚操作宏 ===================== */
#define SCL_HIGH()   GPIO_SetBits(GPIOB, GPIO_Pin_6)
#define SCL_LOW()    GPIO_ResetBits(GPIOB, GPIO_Pin_6)
#define SDA_HIGH()   GPIO_SetBits(GPIOB, GPIO_Pin_7)
#define SDA_LOW()    GPIO_ResetBits(GPIOB, GPIO_Pin_7)
#define SDA_READ()   GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_7)

/* ===================== 延时（72MHz下约5us） ===================== */
static void I2C_Delay(void)
{
    uint32_t i = 72;
    while (i--);
}

/**
  * @brief  I2C初始化
  *         PB6=SCL，PB7=SDA，开漏输出，需外部接上拉电阻（4.7kΩ到3.3V）
  */
void I2C_SW_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_OD;   // 开漏输出
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    /* 空闲状态：SCL和SDA均为高电平 */
    SCL_HIGH();
    SDA_HIGH();
}

/**
  * @brief  发送起始信号
  *         SCL高电平期间，SDA产生下降沿
  */
void I2C_SW_Start(void)
{
    SDA_HIGH();
    I2C_Delay();
    SCL_HIGH();
    I2C_Delay();
    SDA_LOW();      // SDA下降沿 = 起始信号
    I2C_Delay();
    SCL_LOW();      // 拉低SCL，准备发送数据
    I2C_Delay();
}

/**
  * @brief  发送停止信号
  *         SCL高电平期间，SDA产生上升沿
  */
void I2C_SW_Stop(void)
{
    SCL_LOW();
    I2C_Delay();
    SDA_LOW();
    I2C_Delay();
    SCL_HIGH();
    I2C_Delay();
    SDA_HIGH();     // SDA上升沿 = 停止信号
    I2C_Delay();
}

/**
  * @brief  等待从机ACK应答
  * @retval 0=收到ACK，1=超时无ACK（已自动发送停止信号）
  */
uint8_t I2C_SW_WaitAck(void)
{
    uint16_t timeout = 5000;   // 加大超时时间

    SDA_HIGH();     // 释放SDA，让从机控制
    I2C_Delay();
    SCL_HIGH();     // 拉高SCL，读取从机应答
    I2C_Delay();

    while (SDA_READ()) {
        if (--timeout == 0) {
            I2C_SW_Stop();
            return 1;   // 超时，从机无应答
        }
    }

    SCL_LOW();
    I2C_Delay();
    return 0;       // 收到ACK
}

/**
  * @brief  主机发送ACK
  *         用于连续读取多字节时，告知从机继续发送
  */
void I2C_SW_SendAck(void)
{
    SDA_LOW();      // ACK = SDA低电平
    I2C_Delay();
    SCL_HIGH();
    I2C_Delay();
    SCL_LOW();
    I2C_Delay();
    SDA_HIGH();
    I2C_Delay();
}

/**
  * @brief  主机发送NACK
  *         用于读取最后一字节后，告知从机停止发送
  */
void I2C_SW_SendNAck(void)
{
    SDA_HIGH();     // NACK = SDA高电平
    I2C_Delay();
    SCL_HIGH();
    I2C_Delay();
    SCL_LOW();
    I2C_Delay();
}

/**
  * @brief  发送一个字节（高位先行）
  * @param  byte: 要发送的字节
  */
void I2C_SW_SendByte(uint8_t byte)
{
    uint8_t i = 8;
    while (i--) {
        SCL_LOW();
        I2C_Delay();
        /* 高位先行，逐位输出 */
        if (byte & 0x80) {
            SDA_HIGH();
        } else {
            SDA_LOW();
        }
        byte <<= 1;
        I2C_Delay();
        SCL_HIGH();     // SCL上升沿，从机采样数据
        I2C_Delay();
    }
    SCL_LOW();
    I2C_Delay();
}

/**
  * @brief  接收一个字节（高位先行）
  * @retval 接收到的字节
  */
uint8_t I2C_SW_ReceiveByte(void)
{
    uint8_t i = 8;
    uint8_t byte = 0;

    SDA_HIGH();     // 释放SDA，准备接收
    while (i--) {
        byte <<= 1;
        SCL_LOW();
        I2C_Delay();
        SCL_HIGH();     // SCL上升沿，主机采样数据
        I2C_Delay();
        if (SDA_READ()) {
            byte |= 0x01;
        }
    }
    SCL_LOW();
    I2C_Delay();
    return byte;
}
