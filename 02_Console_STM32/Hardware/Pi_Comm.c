/**
  ******************************************************************************
  * @file    Pi_Comm.c
  * @brief   树莓派通信协议层
  *
  *  【接收协议】树莓派 → STM32（文本格式，\n结尾）
  *    D:120.50                          仅距离（模式1实时）
  *    D:120.50,shape:circle,x:8.30      形状数据（模式2）
  *    D:120.50,N:2,x:7.50               正方形数据（模式3/4/5）
  *    STATUS:mode=3,status=2            状态确认
  *    ERROR:No A4 detected              错误信息
  *
  *  【发送协议】STM32 → 树莓派（文本格式，\n结尾）
  *    MODE:0\n    切换到空闲模式
  *    MODE:1\n    切换到实时测距
  *    MODE:2\n    切换到形状识别
  *    MODE:3\n    切换到多正方形
  *    MODE:4\n    切换到带编号正方形
  *    MODE:5\n    切换到倾斜板模式
  *    TRIGGER\n   触发一次测量
  *    D=xxx\n     设置目标距离（树莓派用于控制小车停止）
  *    N=x\n       设置目标编号
  ******************************************************************************
  */

#include "Pi_Comm.h"
#include "USART2.h"
#include "HMI.h"
#include "main.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// 解析结果缓存
static PiData_t s_latest_data = {0};
static volatile uint8_t s_data_updated = 0;
volatile uint8_t g_pi_mode = 0xFF;

/* 多正方形结果（模式3/4） */
volatile MultiSquareData_t g_multi_squares = {0};
volatile uint8_t g_multi_updated = 0;
volatile uint8_t g_debug_count_at_status = 0;  // 调试：STATUS到来时count的值

/**
  * @brief  初始化树莓派通信
  */
void PiComm_Init(void)
{
    USART2_Init();
    memset(&s_latest_data, 0, sizeof(PiData_t));
    s_data_updated = 0;
}

/**
  * @brief  向树莓派发送模式切换指令
  * @param  mode: 0=空闲, 1=测距, 2=形状, 3=多正方形, 4=带编号, 5=倾斜
  */
void PiComm_SendMode(uint8_t mode)
{
    char buf[16];
    sprintf(buf, "MODE:%d\n", mode);
    USART2_SendString(buf);
}

/**
  * @brief  向树莓派发送触发测量指令
  */
void PiComm_SendTrigger(void)
{
    USART2_SendString("TRIGGER\n");
}

/**
  * @brief  向树莓派发送目标距离（用于告知树莓派小车停止距离）
  * @param  dist_cm: 目标距离（cm）
  */
void PiComm_SendTargetDist(uint16_t dist_cm)
{
    char buf[16];
    sprintf(buf, "D=%d\n", dist_cm);
    USART2_SendString(buf);
}

/**
  * @brief  向树莓派发送目标编号（发挥题指定正方形编号）
  * @param  num: 正方形编号（1-8）
  */
void PiComm_SendTargetNum(uint8_t num)
{
    char buf[12];
    sprintf(buf, "N=%d\n", num);
    USART2_SendString(buf);
}

/**
  * @brief  解析一行来自树莓派的数据
  *         格式：D:120.50,N:2,x:7.50
  * @param  line: 以\0结尾的字符串
  * @param  out:  解析结果输出
  * @retval 1=解析成功，0=失败
  */
static uint8_t _ParseLine(const char *line, PiData_t *out)
{
    if (!line || !out) return 0;

    memset(out, 0, sizeof(PiData_t));
    out->valid = 0;

    /* 二进制帧和文本混发：先在缓冲区里找有效文本的起始位置
     * 跳过所有非打印字符（二进制帧头、校验和等），找到第一个可打印字符 */
    while (*line && (uint8_t)*line < 0x20) {
        line++;
    }
    if (*line == '\0') return 0;

    /* STATUS行：解析mode值，更新g_pi_mode（在PiComm_Process里统一处理，这里直接返回） */
    if (strncmp(line, "STATUS:", 7) == 0) return 0;
    /* ERROR行：忽略 */
    if (strncmp(line, "ERROR:",  6) == 0) return 0;

    /* 在当前行里搜索 "D:" 起始位置（二进制帧可能混在前面） */
    const char *p_d = strstr(line, "D:");
    if (!p_d) return 0;
    line = p_d;

    /* 解析距离 */
    out->distance = atof(line + 2);

    /* 查找 shape 字段 */
    const char *p_shape = strstr(line, "shape:");
    if (p_shape) {
        p_shape += 6;
        if      (strncmp(p_shape, "circle",   6) == 0) out->shape_type = SHAPE_CIRCLE;
        else if (strncmp(p_shape, "triangle", 8) == 0) out->shape_type = SHAPE_TRIANGLE;
        else if (strncmp(p_shape, "square",   6) == 0) out->shape_type = SHAPE_SQUARE;
    }

    /* 查找 N 字段（正方形编号） */
    const char *p_n = strstr(line, ",N:");
    if (p_n) {
        out->square_num = (uint8_t)atoi(p_n + 3);
    }

    /* 查找 x 字段（边长/直径） */
    const char *p_x = strstr(line, ",x:");
    if (p_x) {
        out->size_cm = atof(p_x + 3);
    }

    out->valid = 1;
    return 1;
}

/**
  * @brief  轮询处理树莓派串口数据（在主循环中调用）
  *         连续处理所有待处理行，避免多行数据只处理第一行
  */
void PiComm_Process(void)
{
    while (USART2_GetRxFlag())
    {
        char *line = (char*)USART2_GetRxBuffer();

        /* 跳过空行 */
        if (line[0] == '\0') {
            USART2_ClearRxBuffer();
            continue;
        }

        /* STATUS行：一批多正方形数据结束 */
        if (strncmp(line, "STATUS:", 7) == 0) {
            const char *p = strstr(line, "mode=");
            if (p) g_pi_mode = (uint8_t)atoi(p + 5);

            /* 把所有检测到的编号拼成字符串发到HMI的N控件，供用户选择 */
            if (g_multi_squares.count > 0) {
                g_debug_count_at_status = g_multi_squares.count;
                g_multi_updated = 1;
                char num_str[32] = {0};
                for (uint8_t i = 0; i < g_multi_squares.count; i++) {
                    char tmp[4];
                    sprintf(tmp, "%d", g_multi_squares.items[i].num);
                    if (i > 0) strcat(num_str, " ");
                    strcat(num_str, tmp);
                }
                HMI_SetText("N", num_str);
                g_multi_squares.count = 0;
            }
            USART2_ClearRxBuffer();
            continue;
        }

        /* 解析测量数据行 */
        PiData_t parsed;
        if (_ParseLine(line, &parsed)) {
            if (parsed.square_num > 0) {
                /* 多正方形模式：累积存储 */
                /* count==0 说明是新一批，重置 */
                if (g_multi_squares.count == 0) {
                    g_multi_squares.distance = parsed.distance;
                    g_multi_updated = 0;
                }
                if (g_multi_squares.count < MAX_SQUARES) {
                    uint8_t i = g_multi_squares.count;
                    g_multi_squares.items[i].num     = parsed.square_num;
                    g_multi_squares.items[i].side_cm = parsed.size_cm;
                    g_multi_squares.count++;
                }
                s_latest_data  = parsed;
                s_data_updated = 1;
            } else {
                /* 单正方形或形状模式：直接覆盖，同时重置多正方形缓存 */
                g_multi_squares.count = 0;
                g_multi_updated = 0;
                /* 模式3若仅回传 D,x（无编号），清空N列表避免显示旧值 */
                HMI_SetText("N", "");
                s_latest_data  = parsed;
                s_data_updated = 1;
            }
        }

        USART2_ClearRxBuffer();
    }
}

/**
  * @brief  检查是否有新的测量数据
  * @retval 1=有新数据，0=无
  */
uint8_t PiComm_HasNewData(void)
{
    if (s_data_updated) {
        s_data_updated = 0;
        return 1;
    }
    return 0;
}

/**
  * @brief  获取最新的测量数据
  * @retval 指向最新数据的指针
  */
const PiData_t* PiComm_GetData(void)
{
    return &s_latest_data;
}

/**
  * @brief  按编号查询边长（从多正方形缓存里找）
  * @param  num: 正方形编号
  * @retval 边长（cm），找不到返回0
  */
float PiComm_QuerySquare(uint8_t num)
{
    for (uint8_t i = 0; i < g_multi_squares.count; i++) {
        if (g_multi_squares.items[i].num == num) {
            return g_multi_squares.items[i].side_cm;
        }
    }
    return 0.0f;
}
