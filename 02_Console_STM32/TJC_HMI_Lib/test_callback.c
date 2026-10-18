/**
  * @file    test_callback.c
  * @brief   回调函数测试用例 - 帮助理解事件回调机制
  *
  *  运行流程：
  *  1. 用户注册回调函数
  *  2. 模拟串口屏发送数据
  *  3. 库解析后自动调用用户的回调函数
  *  4. 用户在回调中处理业务
  */

#include <stdio.h>
#include "tjc_hmi.h"

/* ===================== 用户定义的 ID 含义 ===================== */
#define MY_ID_DISTANCE    1    // 距离
#define MY_ID_NUMBER      2    // 编号
#define MY_ID_BUTTON_OK   1    // 确认按钮

/* ===================== 用户的业务变量 ===================== */
volatile uint16_t target_D = 0;
volatile uint8_t  target_N = 0;
volatile uint8_t  button_pressed = 0;

/* ===================== 用户的回调函数（自己写！） ===================== */
/**
  * @brief  这个函数是用户自己写的，库会在收到数据时自动调用它
  * @param  event: 库解析好的事件数据
  *
  *  库通过 _TriggerEvent() 调用这个函数，用户不需要关心谁调用
  *  用户只需要在这个函数里写业务逻辑
  */
void MyHMI_Handler(const TJC_Event_t *event)
{
    printf("[回调触发] type=%d, id=%d, value=%d\n", 
           event->type, event->id, event->value);

    switch (event->type) {
        case TJC_EVENT_BUTTON:
            // 按钮事件：只有 id，没有数据
            printf("[按钮] 按钮 %d 被按下\n", event->id);
            if (event->id == MY_ID_BUTTON_OK) {
                button_pressed = 1;
                printf("[业务] 确认按钮按下！\n");
            }
            break;

        case TJC_EVENT_DATA:
            // 数据事件：有 id 和 value
            printf("[数据] id=%d, value=%d\n", event->id, event->value);
            if (event->id == MY_ID_DISTANCE) {
                target_D = event->value;
                printf("[业务] 设置距离 target_D = %d cm\n", target_D);
            } else if (event->id == MY_ID_NUMBER) {
                target_N = event->value;
                printf("[业务] 设置编号 target_N = %d\n", target_N);
            }
            break;

        default:
            printf("[未知事件] type=%d\n", event->type);
            break;
    }
}

/* ===================== 模拟测试 ===================== */
/**
  * @brief  模拟串口屏发送数据
  * @note   实际使用中，这个函数在 USART1_IRQHandler 中调用
  */
void SimulateScreenSend(const uint8_t *data, uint8_t len)
{
    printf("\n[模拟发送] ");
    for (uint8_t i = 0; i < len; i++) {
        printf("%02X ", data[i]);
    }
    printf("\n");

    // 逐字节喂给库
    for (uint8_t i = 0; i < len; i++) {
        TJC_HMI_RxByte(data[i]);
    }
}

/* ===================== 主函数 ===================== */
int main(void)
{
    printf("=== 回调函数测试用例 ===\n\n");

    // 第1步：初始化库
    printf("[初始化] 注册回调函数 MyHMI_Handler\n");
    TJC_HMI_Init(9600);
    TJC_HMI_RegisterCallback(MyHMI_Handler);

    // 第2步：模拟测试
    printf("\n--- 测试1：按钮按下 ---\n");
    // 屏幕发送：A5 01 5A（按钮1按下）
    uint8_t btn_data[] = {0xA5, 0x01, 0x5A};
    SimulateScreenSend(btn_data, 3);

    printf("\n--- 测试2：输入距离 150 ---\n");
    // 屏幕发送：A5 01 31 35 30 5A（距离=150）
    uint8_t dist_data[] = {0xA5, 0x01, 0x31, 0x35, 0x30, 0x5A};
    SimulateScreenSend(dist_data, 6);

    printf("\n--- 测试3：输入编号 2 ---\n");
    // 屏幕发送：A5 02 32 5A（编号=2）
    uint8_t num_data[] = {0xA5, 0x02, 0x32, 0x5A};
    SimulateScreenSend(num_data, 4);

    // 第3步：查看结果
    printf("\n=== 最终结果 ===\n");
    printf("target_D = %d cm\n", target_D);
    printf("target_N = %d\n", target_N);
    printf("button_pressed = %d\n", button_pressed);

    return 0;
}
