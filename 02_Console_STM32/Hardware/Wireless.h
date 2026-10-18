#ifndef __WIRELESS_H
#define __WIRELESS_H

#include <stdint.h>

// 无线指令类型
typedef enum {
    WIRELESS_CMD_SET_DIST = 0x01,  // 设置目标距离
    WIRELESS_CMD_START    = 0x02,  // 启动小车
    WIRELESS_CMD_STOP     = 0x03,  // 停止小车
    WIRELESS_CMD_FEEDBACK = 0x04    // 小车反馈
} WirelessCmd_t;

// 发送目标距离给小车（编码器定距模式）
void Wireless_SendDistance(uint16_t distance_cm);

// 发送"启动前进"指令给小车
void Wireless_SendStart(void);

// 发送"停车"指令给小车
void Wireless_SendStop(void);

// 实时发送当前距离给小车（保留，暂不使用）
void Wireless_SendActualDistance(uint16_t distance_cm);

// 接收小车反馈
uint8_t Wireless_ReceiveFeedback(uint8_t* feedback_data, uint8_t* len);

#endif