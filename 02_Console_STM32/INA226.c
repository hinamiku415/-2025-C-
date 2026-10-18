/**
  ******************************************************************************
  * @file    INA226.c
  * @brief   INA226 电流/电压/功率检测驱动（标准库，软件I2C）
  *
  *  两条独立的电流计算路径：
  *
  *  路径A（I）：芯片内部计算
  *    calib = 0.00512 / (current_lsb × R_shunt_nominal)
  *    电流寄存器 = 芯片用calib算好的值
  *    ReadCurrent_mA = 电流寄存器 × current_lsb × 1000
  *    → 依赖 calib 写入正确，calib 必须 ≤ 32767，否则溢出
  *
  *  路径B（Raw）：软件直接计算
  *    ReadShuntVoltage_uV = 分流电压寄存器 × 2.5μV
  *    ReadCurrent_Raw_mA  = shunt_uV / R_real × 校正系数
  *    → 完全不依赖 calib，只要分流电压寄存器能读到值就能算
  *
  *  当前硬件情况：
  *    R8标称5mΩ，实测约2.4mΩ（PCB并联路径或焊错料）
  *    用 R_shunt_nominal=0.005 保证 calib 不溢出
  *    用 RAW_CORRECTION=2.08 补偿 Raw 路径的阻值偏差
  *    I 路径通过 max_current_A 调整 current_lsb 使读数正确
  ******************************************************************************
  */

#include "INA226.h"
#include "I2C.h"
#include "Delay.h"

/* 电流LSB（A/bit），由初始化时计算 */
static float s_current_lsb = 0.0f;

/* 调试用：校准寄存器写入值和读回值 */
uint16_t g_ina226_calib_written  = 0;
uint16_t g_ina226_calib_readback = 0;

/**
  * @brief  向INA226写入一个16位寄存器
  * @param  reg: 寄存器地址
  * @param  val: 要写入的16位值（高字节先发）
  */
static void WriteReg(uint8_t reg, uint16_t val)
{
    I2C_SW_Start();
    I2C_SW_SendByte(INA226_I2C_ADDR << 1);     // 写模式（最低位为0）
    I2C_SW_WaitAck();
    I2C_SW_SendByte(reg);                       // 寄存器地址
    I2C_SW_WaitAck();
    I2C_SW_SendByte((val >> 8) & 0xFF);         // 高字节
    I2C_SW_WaitAck();
    I2C_SW_SendByte(val & 0xFF);                // 低字节
    I2C_SW_WaitAck();
    I2C_SW_Stop();
}

/**
  * @brief  从INA226读取一个16位寄存器
  * @param  reg: 寄存器地址
  * @retval 读取到的16位值
  */
static uint16_t ReadReg(uint8_t reg)
{
    uint8_t msb, lsb;

    /* 第一步：写寄存器地址（指针寄存器） */
    I2C_SW_Start();
    I2C_SW_SendByte(INA226_I2C_ADDR << 1);     // 写模式
    I2C_SW_WaitAck();
    I2C_SW_SendByte(reg);                       // 指定要读的寄存器
    I2C_SW_WaitAck();
    I2C_SW_Stop();

    Delay_ms(1);    // 等待转换完成

    /* 第二步：重新起始，切换为读模式，读取2字节 */
    I2C_SW_Start();
    I2C_SW_SendByte((INA226_I2C_ADDR << 1) | 0x01);  // 读模式（最低位为1）
    I2C_SW_WaitAck();
    msb = I2C_SW_ReceiveByte();     // 高字节
    I2C_SW_SendAck();               // 告知从机继续发送
    lsb = I2C_SW_ReceiveByte();     // 低字节
    I2C_SW_SendNAck();              // 告知从机停止发送
    I2C_SW_Stop();

    return ((uint16_t)msb << 8) | lsb;
}

/**
  * @brief  INA226初始化
  * @param  max_current_A: 最大测量电流（A），建议设1.0（留余量）
  * @param  shunt_ohm:     采样电阻阻值（Ω），10mΩ填0.01
  */
void INA226_Init(void)
{
    I2C_SW_Init();

    /* 用标称阻值和最大电流计算 calib，确保不溢出 */
    float current_lsb = INA226_MAX_CURRENT_A / 32768.0f;
    s_current_lsb = current_lsb;

    uint16_t calib = (uint16_t)(0.00512f / (current_lsb * INA226_SHUNT_NOMINAL_OHM));
    /* calib 溢出保护：若超过 32767 说明参数配置有误 */
    if (calib > 32767) calib = 32767;

    /* 写配置寄存器 */
    WriteReg(INA226_REG_CONFIG, INA226_CONFIG_VAL);
    Delay_ms(5);

    /* 写校准寄存器 */
    WriteReg(INA226_REG_CALIB, calib);
    Delay_ms(5);

    /* 读回验证 */
    g_ina226_calib_written  = calib;
    g_ina226_calib_readback = ReadReg(INA226_REG_CALIB);
}

/**
  * @brief  读取电流
  * @retval 电流（mA），负值表示反向电流
  */
float INA226_ReadCurrent_mA(void)
{
    /* I路径：芯片内部用calib算好，直接读电流寄存器
     * current_lsb 由 MAX_CURRENT_A/32768 决定
     * 若读数偏小K倍，将 INA226_MAX_CURRENT_A 除以K */
    int16_t raw = (int16_t)ReadReg(INA226_REG_CURRENT);
    return raw * s_current_lsb * 1000.0f;
}

/**
  * @brief  读取总线电压（IN-引脚到GND的电压，即负载两端电压）
  * @retval 电压（V），分辨率1.25mV
  */
float INA226_ReadVoltage_V(void)
{
    uint16_t raw = ReadReg(INA226_REG_BUSV);
    return raw * 0.00125f;      // 1.25mV/LSB
}

uint16_t INA226_ReadVoltageRaw(void)
{
    return ReadReg(INA226_REG_BUSV);
}

/**
  * @brief  读取功率
  * @retval 功率（mW）
  *         功率寄存器LSB = 25 × current_lsb
  */
float INA226_ReadPower_mW(void)
{
    uint16_t raw = ReadReg(INA226_REG_POWER);
    /* 功率寄存器 LSB = 25 × current_lsb，与 I 路径同步 */
    return raw * 25.0f * s_current_lsb * 1000.0f;
}

/**
  * @brief  读取原始分流电压（不依赖校准寄存器，调试用）
  * @retval 分流电压（μV），1LSB = 2.5μV
  *
  *  用途：当电流寄存器读数异常时，用此函数验证硬件连接是否正确。
  *  正常情况下：shunt_uV = 实际电流(A) × 采样电阻(Ω) × 1e6
  *  例如：500mA × 0.005Ω = 2500μV
  */
float INA226_ReadShuntVoltage_uV(void)
{
    /* 分流电压寄存器是有符号16位，1LSB = 2.5μV */
    int16_t raw = (int16_t)ReadReg(INA226_REG_SHUNTV);
    return raw * 2.5f;
}

/**
  * @brief  由分流电压直接计算电流（不依赖校准寄存器，调试用）
  * @retval 电流（mA）
  *
  *  公式：I = V_shunt / R_shunt
  *  此函数完全绕过校准寄存器，可用于验证校准值是否正确。
  */
float INA226_ReadCurrent_Raw_mA(void)
{
    /* Raw路径：直接读分流电压，除以标称阻值，乘以校正系数
     * RAW_CORRECTION 补偿实际阻值与标称值的偏差 */
    float shunt_uv = INA226_ReadShuntVoltage_uV();
    return (shunt_uv / INA226_SHUNT_NOMINAL_OHM) / 1000.0f * INA226_RAW_CORRECTION;
}
