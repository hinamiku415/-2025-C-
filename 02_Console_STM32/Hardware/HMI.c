/**
  ******************************************************************************
  * @file    HMI.c
  * @brief   HMI串口屏驱动（USART1，9600bps）
  *
  *  【接收协议】HMI屏 → STM32
  *    单字节 0x00        → 强制切换到 STATE_IDLE
  *    单字节 0x81~0x87   → 切换状态机（见下表，避开TJC返回包范围0x01~0x1F）
  *      0x81 = STATE_BASIC_1
  *      0x82 = STATE_BASIC_2
  *      0x83 = STATE_BASIC_34
  *      0x84 = STATE_ADV_1
  *      0x85 = STATE_ADV_2
  *      0x86 = STATE_ADV_3
  *      0x87 = STATE_ADV_45
  *    文本行（\n结尾）   → 解析参数指令（需先发包头）
  *      0xAA + 数字 + \r\n  → 设置 target_D（目标距离，cm）
  *      0xBB + 数字 + \r\n  → 设置 target_N（目标正方形编号）  *
  *  【发送协议】STM32 → HMI屏（Nextion格式，0xFF 0xFF 0xFF结尾）
  *    obj.txt="value"    → 设置文本控件内容
  ******************************************************************************
  */

#include "stm32f10x.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "Serial.h"
#include "HMI.h"
#include "main.h"

/* ===================== 接收缓冲区 ===================== */
#define UART_BUF_SIZE 64
static uint8_t UART_RxBuf[UART_BUF_SIZE];
static uint8_t UART_RxCounter = 0;
volatile uint8_t UART_DataReady = 0;

/* 当前帧类型：0=未知，0xAA=距离帧，0xBB=编号帧 */
static uint8_t s_frame_type = 0;

/* ===================== 调试缓冲区（记录最近收到的原始字节） ===================== */
/* 开关在 HMI.h 中：#define HMI_DEBUG_RX                                          */
/* OLED第4行会显示最近4个字节的十六进制值，格式：44 3D 35 35                       */

#ifdef HMI_DEBUG_RX
static uint8_t dbg_buf[8] = {0};   // 滚动记录最近8个字节
static uint8_t dbg_idx = 0;
#endif

/**
  * @brief  USART1中断服务函数（HMI串口屏接收）
  *
  *  同时处理三类数据：
  *  1. 0x00          → 强制复位到 IDLE
  *  2. 0x01~0x14     → 单字节状态切换指令（HMI按钮事件）
  *  3. 文本行\n结尾  → 参数设置指令（D=xxx / N=x）
  */
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
        uint8_t res = USART_ReceiveData(USART1);

#ifdef HMI_DEBUG_RX
        /* 滚动记录原始字节，供 App_UpdateOLED 显示 */
        dbg_buf[dbg_idx & 0x07] = res;
        dbg_idx++;
#endif

        /* --- 类型1：强制复位 --- */
        if (res == 0x00) {
            currentState = STATE_IDLE;
            UART_RxCounter = 0;
            UART_DataReady = 0;
            /* HMI屏复位后控件清零，重新推送当前测量结果 */
            UI_UpdateResult(g_dist_d, g_width_x);
            USART_ClearITPendingBit(USART1, USART_IT_RXNE);
            return;
        }

        /* --- 类型2：单字节状态切换指令 --- */
        if (res >= 0x01 && res <= 0x14) {
            switch (res) {
                case 0x01: currentState = STATE_BASIC_1;  break;
                case 0x02: currentState = STATE_BASIC_2;  break;
                case 0x03: currentState = STATE_BASIC_34; break;
                case 0x11: currentState = STATE_ADV_1;    break;
                case 0x12: currentState = STATE_ADV_2;    break;
                case 0x13: currentState = STATE_ADV_3;    break;
                case 0x14: currentState = STATE_ADV_45;   break;
                default: break;
            }
            UART_RxCounter = 0;
            UART_DataReady = 0;
            USART_ClearITPendingBit(USART1, USART_IT_RXNE);
            return;
        }

        /* --- Nextion触摸事件包头（0x65），整包丢弃 --- */
        if (res == 0x65) {
            UART_RxCounter = 0;
            UART_DataReady = 0;
            USART_ClearITPendingBit(USART1, USART_IT_RXNE);
            return;
        }

        /* --- 类型3：包头标识（0xAA=距离帧，0xBB=编号帧） --- */
        if (res == 0xAA || res == 0xBB) {
            s_frame_type = res;
            UART_RxCounter = 0;
            USART_ClearITPendingBit(USART1, USART_IT_RXNE);
            return;
        }

        /* --- 类型4：结尾符0xCC，触发解析 --- */
        if (res == 0xCC) {
            if (UART_RxCounter > 0 && s_frame_type != 0) {
                UART_RxBuf[UART_RxCounter] = '\0';
                if (s_frame_type == 0xAA) {
                    target_D = (uint16_t)atoi((char*)UART_RxBuf);
                } else if (s_frame_type == 0xBB) {
                    target_N = (uint8_t)atoi((char*)UART_RxBuf);
                }
                g_param_updated = 1;
                s_frame_type = 0;
            }
            UART_RxCounter = 0;
            USART_ClearITPendingBit(USART1, USART_IT_RXNE);
            return;
        }

        /* --- 类型5：数字字节，存入缓冲区 --- */
        if (res == '\n' || res == '\r' || res == 0xFF) {
            /* 忽略，不作为结尾符 */
        } 
        else {
            if (UART_RxCounter < UART_BUF_SIZE - 1) {
                UART_RxBuf[UART_RxCounter++] = res;
            } else {
                UART_RxCounter = 0;
            }
        }

        USART_ClearITPendingBit(USART1, USART_IT_RXNE);
    }
}

/**
  * @brief  解析HMI发来的文本参数指令（保留接口，实际解析已在中断里完成）
  */
void Command_Parser(void)
{
    /* 解析逻辑已移入 USART1_IRQHandler，此函数保留供主循环调用，无需操作 */
    UART_DataReady = 0;  // 确保标志清零
}

/**
  * @brief  更新功耗显示
  * @param  ma: 电流（mA）
  * @param  mw: 功耗（mW）
  */
void UI_UpdatePower(int ma, int mw)
{
    char buf[32];
    sprintf(buf, "Is:%dmA", ma);
    HMI_SetText("t_Is", buf);
    sprintf(buf, "P:%dmW", mw);
    HMI_SetText("t_P", buf);
    sprintf(buf, "Pm:%dmW", g_power_max_mw);
    HMI_SetText("t_Pm", buf);
}

/**
  * @brief  更新测量结果显示（含正方形编号）
  * @param  d:   距离（cm）
  * @param  x:   边长/直径（cm）
  * @param  num: 正方形编号（0=无编号）
  */
void UI_UpdateResult(float d, float x)
{
    char buf[32];
    sprintf(buf, "%05.1fcm", d);
    HMI_SetText("D", buf);
    sprintf(buf, "%05.1fcm", x);
    HMI_SetText("X", buf);
}

void UI_UpdateResultWithNum(float d, float x, uint8_t num)
{
    UI_UpdateResult(d, x);
    /* N控件由多正方形列表显示逻辑负责，这里不覆盖 */
    /* 仅在单编号模式（模式4/5）下更新N控件 */
    (void)num;
}

/**
  * @brief  发送一条HMI指令（自动追加0xFF 0xFF 0xFF结束符）
  * @param  cmd: 指令字符串，如 "t0.txt=\"hello\""
  */
void HMI_SendCmd(char *cmd)
{
    printf("%s", cmd);
    putchar(0xFF);
    putchar(0xFF);
    putchar(0xFF);
}

/**
  * @brief  设置HMI文本控件内容
  * @param  obj:   控件名，如 "D"
  * @param  value: 文本内容
  */
void HMI_SetText(char *obj, char *value)
{
    char buf[64];
    sprintf(buf, "%s.txt=\"%s\"", obj, value);
    HMI_SendCmd(buf);
}

/**
  * @brief  获取调试用的最近4个原始接收字节（HMI_DEBUG_RX开启时有效）
  * @param  out: 输出数组，长度4
  */
void HMI_GetDebugBytes(uint8_t out[4])
{
#ifdef HMI_DEBUG_RX
    /* 取最近8字节中最早的4个（即第一批到达的字节）*/
    uint8_t start = (dbg_idx) & 0x07;  // 最旧的字节位置
    out[0] = dbg_buf[(start + 0) & 0x07];
    out[1] = dbg_buf[(start + 1) & 0x07];
    out[2] = dbg_buf[(start + 2) & 0x07];
    out[3] = dbg_buf[(start + 3) & 0x07];
#else
    out[0] = out[1] = out[2] = out[3] = 0;
#endif
}
