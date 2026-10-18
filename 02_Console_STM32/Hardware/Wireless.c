#include "Wireless.h"
#include "NRF24L01.h"
#include <string.h>

// 发送目标距离给小车（用于编码器定距模式）
// 帧格式：[0xAA][0xBB][距离低8位][距离高8位]
void Wireless_SendDistance(uint16_t distance_cm)
{
    NRF24L01_TxPacket[0] = 0xAA;
    NRF24L01_TxPacket[1] = 0xBB;
    NRF24L01_TxPacket[2] = distance_cm & 0xFF;
    NRF24L01_TxPacket[3] = (distance_cm >> 8) & 0xFF;
    NRF24L01_Send();
}

// 发送"启动前进"指令给小车
// 帧格式：[0xAA][0xDD][0x01][0x00]
void Wireless_SendStart(void)
{
    NRF24L01_TxPacket[0] = 0xAA;
    NRF24L01_TxPacket[1] = 0xDD;
    NRF24L01_TxPacket[2] = 0x01;   // 启动
    NRF24L01_TxPacket[3] = 0x00;
    NRF24L01_Send();
}

// 发送"停车"指令给小车
// 帧格式：[0xAA][0xDD][0x00][0x00]
void Wireless_SendStop(void)
{
    NRF24L01_TxPacket[0] = 0xAA;
    NRF24L01_TxPacket[1] = 0xDD;
    NRF24L01_TxPacket[2] = 0x00;   // 停车
    NRF24L01_TxPacket[3] = 0x00;
    NRF24L01_Send();
}

// 实时发送当前距离给小车（保留，暂不使用）
void Wireless_SendActualDistance(uint16_t distance_cm)
{
    NRF24L01_TxPacket[0] = 0xAA;
    NRF24L01_TxPacket[1] = 0xCC;
    NRF24L01_TxPacket[2] = distance_cm & 0xFF;
    NRF24L01_TxPacket[3] = (distance_cm >> 8) & 0xFF;
    NRF24L01_Send();
}

// 接收小车反馈
uint8_t Wireless_ReceiveFeedback(uint8_t* feedback_data, uint8_t* len)
{
    uint8_t ret = NRF24L01_Receive();

    if (ret == 1) {
        if (NRF24L01_RxPacket[0] == 0xAA && NRF24L01_RxPacket[1] == 0xBB) {
            *len = 2;
            feedback_data[0] = NRF24L01_RxPacket[2];
            feedback_data[1] = NRF24L01_RxPacket[3];
            return 1;
        }
    }

    return 0;
}