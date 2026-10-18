/**
  ******************************************************************************
  * @file    Pi_Comm.h
  * @brief   树莓派通信协议层头文件
  ******************************************************************************
  */

#ifndef __PI_COMM_H
#define __PI_COMM_H

#include "stm32f10x.h"

/* 形状类型枚举 */
typedef enum {
    SHAPE_NONE     = 0,
    SHAPE_CIRCLE   = 1,
    SHAPE_TRIANGLE = 2,
    SHAPE_SQUARE   = 3
} ShapeType_t;

/* 单个正方形数据 */
typedef struct {
    uint8_t num;      // 编号
    float   side_cm;  // 边长
} SquareItem_t;

/* 树莓派测量数据结构体 */
typedef struct {
    uint8_t     valid;          // 数据是否有效
    float       distance;       // 距离D（cm）
    ShapeType_t shape_type;     // 形状类型（模式2）
    uint8_t     square_num;     // 正方形编号（单个，模式4/5）
    float       size_cm;        // 边长或直径（cm）
} PiData_t;

/* 多正方形结果（模式3：检测到的所有正方形） */
#define MAX_SQUARES  8
typedef struct {
    uint8_t      count;              // 检测到的正方形数量
    float        distance;           // 距离D（cm）
    SquareItem_t items[MAX_SQUARES]; // 各正方形编号和边长
} MultiSquareData_t;

extern volatile MultiSquareData_t g_multi_squares;
extern volatile uint8_t g_multi_updated;
extern volatile uint8_t g_debug_count_at_status;

/* 初始化 */
void PiComm_Init(void);

/* 发送指令给树莓派 */
void PiComm_SendMode(uint8_t mode);
void PiComm_SendTrigger(void);
void PiComm_SendTargetDist(uint16_t dist_cm);
void PiComm_SendTargetNum(uint8_t num);

/* 接收处理（在主循环中调用） */
void PiComm_Process(void);
uint8_t PiComm_HasNewData(void);
const PiData_t* PiComm_GetData(void);

/* 按编号查询边长（从g_multi_squares里找），找不到返回0 */
float PiComm_QuerySquare(uint8_t num);

/* 树莓派最新反馈的模式（由STATUS:mode=x解析，0xFF=未收到过） */
extern volatile uint8_t g_pi_mode;

#endif /* __PI_COMM_H */
