#include "stm32f10x.h"
#include "main.h"

/**
  * @brief  毫秒级延时（基于 SysTick 中断计数 g_tick）
  * @note   依赖 SysTick_Config(SystemCoreClock/1000) 已在 App_Init 中配置
  *         不再直接操作 SysTick 寄存器，避免破坏中断配置
  * @param  xms 延时时长（ms），范围：0~4294967295
  */
void Delay_ms(uint32_t xms)
{
    uint32_t start = g_tick;
    while ((g_tick - start) < xms);
}

/**
  * @brief  微秒级延时（软件循环，粗略实现）
  * @note   STM32F103 @ 72MHz，每次循环约 4 个指令周期 ≈ 56ns
  *         xus=1000 约等于 1ms，精度受编译优化影响
  *         如需精确微秒延时，可用定时器实现
  * @param  xus 延时时长（us），范围：0~65535
  */
void Delay_us(uint32_t xus)
{
    uint32_t i = xus * 18;   // 72MHz / 4指令 = 18次循环 ≈ 1us
    while (i--);
}

/**
  * @brief  秒级延时
  * @param  xs 延时时长（s）
  */
void Delay_s(uint32_t xs)
{
    while (xs--)
    {
        Delay_ms(1000);
    }
}
