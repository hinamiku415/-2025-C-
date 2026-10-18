/**
  ******************************************************************************
  * @file    INA226.h
  * @brief   INA226 电流/电压/功率检测驱动头文件
  *
  *  硬件连接：
  *    VBS  → 5V总线正极（被测电源正端）
  *    IN+  → 采样电阻上端（靠近电源侧）
  *    IN-  → 采样电阻下端（靠近负载侧）
  *    GND  → 共地
  *    SDA  → PB7
  *    SCL  → PB6
  *    A0/A1→ GND（I2C地址0x40）
  *
  *  参数配置：
  *    采样电阻：10mΩ（0.01Ω）
  *    最大电流：1A
  *    总线电压：5V
  ******************************************************************************
  */

#ifndef __INA226_H
#define __INA226_H

#include "stm32f10x.h"

/* I2C地址（A0/A1均接GND时为0x40） */
#define INA226_I2C_ADDR     0x40

/* 寄存器地址 */
#define INA226_REG_CONFIG   0x00    // 配置寄存器
#define INA226_REG_SHUNTV   0x01    // 分流电压寄存器（原始ADC值）
#define INA226_REG_BUSV     0x02    // 总线电压寄存器，分辨率1.25mV/LSB
#define INA226_REG_POWER    0x03    // 功率寄存器
#define INA226_REG_CURRENT  0x04    // 电流寄存器（需先写校准寄存器）
#define INA226_REG_CALIB    0x05    // 校准寄存器

/* ===================== 校准参数 =====================
 *
 *  INA226 有两条独立的电流计算路径，参数分开配置：
 *
 *  【路径A：I（芯片内部计算）】
 *    calib = 0.00512 / (current_lsb × SHUNT_NOMINAL_OHM)
 *    current_lsb = MAX_CURRENT_A / 32768
 *    约束：calib 必须 ≤ 32767，否则 uint16_t 溢出，读数乱掉
 *    约束：MAX_CURRENT_A × SHUNT_NOMINAL_OHM ≥ 0.00512/32767 ≈ 156nΩ（很容易满足）
 *    约束：calib = 0.00512/(MAX_CURRENT_A/32768×SHUNT_NOMINAL_OHM) ≤ 32767
 *          → MAX_CURRENT_A ≥ 0.00512×32768/32767/SHUNT_NOMINAL_OHM
 *          → 对于 SHUNT=0.005Ω：MAX_CURRENT_A ≥ 1.024A，取 1.1f 即可
 *
 *  【路径B：Raw（软件直接算）】
 *    Raw = shunt_uV / SHUNT_NOMINAL_OHM / 1000 × RAW_CORRECTION
 *    RAW_CORRECTION = 实际电流 / (shunt_uV / SHUNT_NOMINAL_OHM / 1000)
 *                   = 实际阻值标称阻值之比的倒数
 *    实测：数控电源445mA，未校正Raw=214mA → RAW_CORRECTION=445/214≈2.08
 *
 *  两条路径用同一个 SHUNT_NOMINAL_OHM，但校正方式不同：
 *    I   路径：通过 MAX_CURRENT_A 控制 current_lsb 来校正
 *    Raw 路径：通过 RAW_CORRECTION 直接乘系数来校正
 * ===================================================*/

/* 采样电阻标称值（Ω）：用于 calib 计算和 Raw 计算的基准
 * R8 = FMF25FPJR005，标称 5mΩ，实测约 2.4mΩ（PCB并联路径）
 * 保持标称值 0.005 以确保 calib 不溢出 */
#define INA226_SHUNT_NOMINAL_OHM    0.005f

/* 最大测量电流（A）：控制 current_lsb，进而控制 I 路径的读数比例
 * 调整方法：若 I 读数 = 实际值 × K，则将此值乘以 K
 * 当前：I路径读数约为实际的1/2 → 改为 1.1/2 = 0.55，取 0.55f
 * 约束：此值不能小于 1.024A（否则 calib 溢出），最小取 1.1f
 *
 * *** 注意：若 I 路径仍不准，优先调整此值，不要改 SHUNT_NOMINAL_OHM ***
 */
#define INA226_MAX_CURRENT_A        1.1f

/* Raw 路径校正系数：补偿实际阻值与标称值的偏差
 * 实测：数控电源445mA，Raw（未校正）=214mA → 2.08
 * 调整方法：CORRECTION = 数控电源读数 / Raw未校正读数 */
#define INA226_RAW_CORRECTION       2.08f

/* 配置寄存器：平均16次，转换时间1.1ms，连续测量分流+总线电压 */
#define INA226_CONFIG_VAL           0x4927

/* 初始化（使用 INA226.h 中的宏参数，无需外部传参） */
void  INA226_Init(void);

float INA226_ReadCurrent_mA(void);      // I路径：芯片内部计算，单位mA
float INA226_ReadVoltage_V(void);       // 总线电压，单位V
uint16_t INA226_ReadVoltageRaw(void);   // 总线电压原始ADC值（调试用）
float INA226_ReadPower_mW(void);        // 功率，单位mW
float INA226_ReadShuntVoltage_uV(void); // 原始分流电压，单位μV（调试用）
float INA226_ReadCurrent_Raw_mA(void);  // Raw路径：软件直接算，单位mA（调试用）

/* 调试用：初始化后校准寄存器的读回值，应等于写入值 */
extern uint16_t g_ina226_calib_written;
extern uint16_t g_ina226_calib_readback;

#endif /* __INA226_H */
