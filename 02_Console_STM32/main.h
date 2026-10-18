/**
  ******************************************************************************
  * @file    main.h
  * @brief   主程序头文件 - 全局状态定义与外部变量声明
  ******************************************************************************
  */

#ifndef __MAIN_H
#define __MAIN_H

#include "stm32f10x.h"

/* ===================== 编译开关 ===================== */
/**
  * ENABLE_WIRELESS：无线通信（NRF24L01 + 小车控制）
  *   #define ENABLE_WIRELESS   → 开启
  *   注释掉                   → 关闭（当前）
  */
// #define ENABLE_WIRELESS

/**
  * ENABLE_PI：树莓派视觉通信（USART3 + Pi_Comm）
  *   #define ENABLE_PI         → 开启
  *   注释掉                   → 关闭（当前）
  *
  * 当前测试阶段：仅验证 HMI串口屏 ↔ STM32 ↔ OLED 这一层
  */
#define ENABLE_PI

/**
  * INA226_TEST：INA226测试模式
  *   开启 → OLED显示电流/电压/功率，屏蔽状态机显示
  *   关闭 → 正常模式（注释掉此行）
  */
//#define INA226_TEST

/* ===================== 系统状态枚举 ===================== */
typedef enum {
    STATE_IDLE     = 0,   // 空闲，等待指令
    STATE_BASIC_1  = 1,   // 基本(1)：循迹1米
    STATE_BASIC_2  = 2,   // 基本(2)：静止测量D和x
    STATE_BASIC_34 = 3,   // 基本(3)(4)：前进到D1/D2停止，测量D和x
    STATE_ADV_1    = 4,   // 发挥(1)：静止测量最小正方形
    STATE_ADV_2    = 5,   // 发挥(2)：前进到D3停止，测量最小正方形
    STATE_ADV_3    = 6,   // 发挥(3)：前进到D4停止，测量指定编号正方形
    STATE_ADV_45   = 7    // 发挥(4)(5)：倾斜板，前进到D5停止
} SystemState_t;

/* ===================== 外部变量声明 ===================== */
extern volatile SystemState_t currentState;
extern uint16_t target_D;       // 目标距离（cm）
extern uint8_t  target_N;       // 目标正方形编号

extern float g_dist_d;          // 测量距离（cm）
extern float g_width_x;         // 测量边长/直径（cm）
extern uint8_t g_square_num;    // 正方形编号（0=无编号）
extern int   g_current_ma;      // 供电电流（mA）
extern int   g_power_mw;        // 功耗（mW）
extern int   g_power_max_mw;    // 最大功耗 Pmax（mW）
extern float g_voltage_v;       // 总线电压（V）

extern volatile uint32_t g_tick; // SysTick计数器（ms）

/* 参数更新标志：HMI解析到新数据后置1，主循环立即刷新OLED */
extern volatile uint8_t g_param_updated;

#endif /* __MAIN_H */
