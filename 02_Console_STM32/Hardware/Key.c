#include "stm32f10x.h"
#include "main.h"

/**
  * @brief  按键初始化
  *         Key1: PA8，上拉输入，低电平触发（一键启动）
  */
void Key_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_8;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
}

/**
  * @brief  非阻塞按键扫描（基于g_tick消抖，在主循环中调用）
  *
  *  状态机：
  *    IDLE    → 检测到低电平 → DEBOUNCE（记录时间）
  *    DEBOUNCE→ 20ms后仍低  → PRESSED（返回键值，进入等待松开）
  *    PRESSED → 检测到高电平 → IDLE
  *
  * @retval 1=Key1触发一次，0=无事件
  */
uint8_t Key_GetNum(void)
{
    typedef enum { KEY_IDLE, KEY_DEBOUNCE, KEY_PRESSED } KeyState_t;
    static KeyState_t state = KEY_IDLE;
    static uint32_t   press_tick = 0;

    uint8_t pin = GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_8);

    switch (state) {
        case KEY_IDLE:
            if (pin == 0) {
                press_tick = g_tick;
                state = KEY_DEBOUNCE;
            }
            break;

        case KEY_DEBOUNCE:
            if (pin == 1) {
                /* 抖动，回到空闲 */
                state = KEY_IDLE;
            } else if (g_tick - press_tick >= 20) {
                /* 稳定低电平超过20ms，确认按下 */
                state = KEY_PRESSED;
                return 1;
            }
            break;

        case KEY_PRESSED:
            /* 等待松开 */
            if (pin == 1) {
                state = KEY_IDLE;
            }
            break;
    }
    return 0;
}
