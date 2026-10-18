/**
  ******************************************************************************
  * @file    main.c
  * @brief   主程序 - 基于单目视觉的运动目标物测量装置（C题）
  *
  *  编译开关（main.h 中配置）：
  *    ENABLE_PI       → 开启树莓派通信（USART3 + Pi_Comm）
  *    ENABLE_WIRELESS → 开启无线通信（NRF24L01 + 小车控制）
  *    INA226_TEST     → INA226测试模式，OLED显示电流/电压/功率
  *
  *  当前测试阶段：INA226_TEST 开启
  ******************************************************************************
  */

/* 在此处控制测试模式，优先级高于 main.h */
/* INA226_TEST 已验证完成，关闭测试模式，恢复正常状态机显示 */
/* #define INA226_TEST */
#define ENABLE_PI
/* 蓝牙方案：小车由树莓派直接通过蓝牙控制，NRF24L01 不再使用 */
/* #define ENABLE_WIRELESS */

#include "stm32f10x.h"
#include "Delay.h"
#include "OLED.h"
#include "Key.h"
#include "Serial.h"
#include "HMI.h"
#include "INA226.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

#ifdef ENABLE_PI
#include "Pi_Comm.h"
#include "USART2.h"
#endif

#ifdef ENABLE_WIRELESS
#include "NRF24L01.h"
#include "Wireless.h"
#endif

/* ===================== 全局变量 ===================== */
volatile SystemState_t currentState = STATE_IDLE;
uint16_t target_D = 0;
uint8_t  target_N = 0;

float g_dist_d    = 0.0f;
float g_width_x   = 0.0f;
uint8_t g_square_num = 0;       // 正方形编号（0=无编号）
int   g_current_ma = 0;
int   g_power_mw   = 0;
int   g_power_max_mw = 0;       // 最大功耗 Pmax，全程记录
float g_voltage_v  = 0.0f;     // 总线电压（V）
volatile uint8_t g_param_updated = 0;  // HMI参数更新标志
uint8_t g_wireless_feedback_count = 0; // 收到小车反馈次数（调试用）


uint16_t g ;


/* ===================== 内部变量 ===================== */
static uint32_t s_last_ui_tick = 0;


/* ===================== 内部函数声明 ===================== */
static void App_Init(void);
static void App_UpdateOLED(void);
static void App_HandleKey(void);

#ifdef ENABLE_PI
static void App_HandlePiData(void);
#endif

#ifdef ENABLE_WIRELESS
static void App_HandleWireless(void);
#endif

/* ===================== 主函数 ===================== */
int main(void)
{
    App_Init();

    while (1)
    {
#ifdef ENABLE_PI
        /* 树莓派数据处理 */
        PiComm_Process();
        App_HandlePiData();
#endif

        /* HMI文本参数指令解析（D=xxx / N=x） */
        Command_Parser();

        /* 按键：一键启动 */
        App_HandleKey();

#ifdef ENABLE_WIRELESS
        /* 无线反馈（小车到达通知） */
        App_HandleWireless();
#endif
        // g = (uint16_t)g_dist_d - target_D;

        /* OLED刷新（200ms一次，或参数更新时立即刷新） */
        if (g_param_updated || g_tick - s_last_ui_tick >= 200) {
            g_param_updated = 0;
            /* 采样INA226：用Raw路径（分流电压直接算），不依赖校准寄存器
         * 电压固定显示标称值（U15地址冲突导致VBUS读数异常，硬件问题留待后续处理） */
            float raw_ma     = INA226_ReadCurrent_Raw_mA();
            g_current_ma = (int)raw_ma;
            g_voltage_v  = 5.09f;
            g_power_mw   = (int)(raw_ma * g_voltage_v);
            /* 更新最大功耗 */
            if (g_power_mw > g_power_max_mw)
                g_power_max_mw = g_power_mw;
            App_UpdateOLED();
            /* 同步更新串口屏电流和功率显示 */
            UI_UpdatePower(g_current_ma, g_power_mw);
            s_last_ui_tick = g_tick;
        }
    }
}

/* ===================== 初始化 ===================== */
static void App_Init(void)
{
    /* 中断优先级分组：全局只配置一次，必须在所有 NVIC_Init 之前 */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    /* SysTick：1ms中断，优先级设为最高（抢占0，响应0），确保 g_tick 正常自增 */
    SysTick_Config(SystemCoreClock / 1000);
    NVIC_SetPriority(SysTick_IRQn, 0x00);

    OLED_Init();
    Key_Init();
    Serial_Init();      // USART1 → HMI串口屏（含 NVIC_PriorityGroupConfig）（始终开启）
    /* 采样电阻R8标称5mΩ，实测约2.4mΩ，参数在INA226.h中配置 */
    INA226_Init();

#ifdef ENABLE_PI
    PiComm_Init();      // USART3 → 树莓派
#endif

#ifdef ENABLE_WIRELESS
    NRF24L01_Init();
#endif

    /* 启动提示：显示当前编译模式 */
    OLED_ShowString(1, 1, "STM32 Ready     ");
#if defined(ENABLE_PI) && defined(ENABLE_WIRELESS)
    OLED_ShowString(2, 1, "Full Mode       ");
#elif defined(ENABLE_PI)
    OLED_ShowString(2, 1, "HMI+Pi Mode     ");
#elif defined(ENABLE_WIRELESS)
    OLED_ShowString(2, 1, "HMI+NRF Mode    ");
#else
    OLED_ShowString(2, 1, "HMI Only        ");
#endif
    Delay_ms(1000);
    OLED_Clear();

    /* 关闭TJC/Nextion屏的指令返回包（bkcmd=0），防止返回字节干扰接收逻辑 */
    /* bkcmd=0: 所有指令执行后不发返回数据 */
    printf("bkcmd=0");
    putchar(0xFF); putchar(0xFF); putchar(0xFF);

#ifdef ENABLE_PI
    PiComm_SendMode(0);  // 通知树莓派进入空闲
#endif
}

/* ===================== OLED状态显示（每200ms无条件刷新） ===================== */
/* ===================== OLED状态显示（每200ms无条件刷新） ===================== */
static void App_UpdateOLED(void)
{
    char buf[20];

#ifdef INA226_TEST
    /* ===== INA226测试模式：显示电流/电压/功率 ===== */
    /* 第1行：校准电流（经校准寄存器计算，受CORRECTION系数影响） */
    sprintf(buf, "I:%6.1fmA     ", (float)g_current_ma);
    OLED_ShowString(1, 1, buf);

    /* 第2行：总线电压 */
    sprintf(buf, "V:%5.3fV       ", g_voltage_v);
    OLED_ShowString(2, 1, buf);

    /* 第3行：功率 */
    sprintf(buf, "P:%6.1fmW     ", (float)g_power_mw);
    OLED_ShowString(3, 1, buf);

    /* 第4行：功率 */
    sprintf(buf, "P:%6.1fmW     ", (float)g_power_mw);
    OLED_ShowString(4, 1, buf);

#else
    /* ===== 正常模式：显示状态机和测量结果 ===== */

    /* 第1行：当前状态名，只在状态变化时更新 */
    static SystemState_t last_state = (SystemState_t)0xFF;
    if (currentState != last_state) {
        last_state = currentState;
        OLED_ShowString(1, 1, "                ");
        switch (currentState) {
            case STATE_IDLE:     OLED_ShowString(1, 1, "IDLE");      break;
            case STATE_BASIC_1:  OLED_ShowString(1, 1, "BASIC 1");   break;
            case STATE_BASIC_2:  OLED_ShowString(1, 1, "BASIC 2");   break;
            case STATE_BASIC_34: OLED_ShowString(1, 1, "BASIC 3&4"); break;
            case STATE_ADV_1:    OLED_ShowString(1, 1, "ADV 1");     break;
            case STATE_ADV_2:    OLED_ShowString(1, 1, "ADV 2");     break;
            case STATE_ADV_3:    OLED_ShowString(1, 1, "ADV 3");     break;
            case STATE_ADV_45:   OLED_ShowString(1, 1, "ADV 4&5");   break;
            default:             OLED_ShowString(1, 1, "???");        break;
        }
    }

    /* 第2行：目标参数（按当前状态显示） */
    switch (currentState) {
        case STATE_IDLE:
        case STATE_BASIC_1:
        case STATE_BASIC_2:
        case STATE_ADV_1:
            OLED_ShowString(2, 1, "Ready           ");
            break;
        case STATE_BASIC_34:
        case STATE_ADV_2:
            sprintf(buf, "D:%3dcm         ", target_D);
            OLED_ShowString(2, 1, buf);
            break;
        case STATE_ADV_3:
        case STATE_ADV_45:
            sprintf(buf, "D:%3dcm N:%d    ", target_D, target_N);
            OLED_ShowString(2, 1, buf);
            break;
        default:
            OLED_ShowString(2, 1, "                ");
            break;
    }

    /* 第3行：测量结果 D、x 和编号（编号为0时不显示） */
    if (g_square_num > 0) {
        sprintf(buf, "D:%.1f x:%.1f N:%d", g_dist_d, g_width_x, g_square_num);
    } else {
        sprintf(buf, "D:%.1f x:%.1f     ", g_dist_d, g_width_x);
    }
    OLED_ShowString(3, 1, buf);

    /* 第4行：无线反馈计数（调试用，收到小车到达反馈时+1） */
    sprintf(buf, "WL rx:%d g:%d    ", g_wireless_feedback_count, g);
    OLED_ShowString(4, 1, buf);

#endif
}

/* ===================== 处理按键（一键启动） ===================== */
static void App_HandleKey(void)
{
    uint8_t key = Key_GetNum();
    if (key != 1) return;

    switch (currentState) {

        case STATE_IDLE:
            /* 空闲状态，按键无动作 */
#ifdef ENABLE_PI
            PiComm_SendMode(0);
#endif
            break;

        case STATE_BASIC_1:
            /* 基础1：小车自己按KEY1跑100cm，发射端不需要发指令 */
            break;

        case STATE_BASIC_2:
#ifdef ENABLE_PI
            PiComm_SendMode(2);
            Delay_ms(100);
            PiComm_SendTrigger();
#endif
            break;

        case STATE_BASIC_34:
#ifdef ENABLE_PI
            PiComm_SendMode(2);
            PiComm_SendTargetDist(target_D);
#endif
#ifdef ENABLE_WIRELESS
            g_dist_d = 0;  // 清零，等树莓派发来新的实时距离再判断
            Wireless_SendStart();
#endif
            break;

        case STATE_ADV_1:
#ifdef ENABLE_PI
            PiComm_SendMode(3);
            Delay_ms(100);
            PiComm_SendTrigger();
#endif
            break;

        case STATE_ADV_2:
#ifdef ENABLE_PI
            PiComm_SendMode(3);
            PiComm_SendTargetDist(target_D);
#endif
#ifdef ENABLE_WIRELESS
            g_dist_d = 0;
            Wireless_SendStart();
#endif
            break;

        case STATE_ADV_3:
#ifdef ENABLE_PI
            PiComm_SendMode(4);
            PiComm_SendTargetNum(target_N);
            PiComm_SendTargetDist(target_D);
#endif
#ifdef ENABLE_WIRELESS
            g_dist_d = 0;
            Wireless_SendStart();
#endif
            break;

        case STATE_ADV_45:
#ifdef ENABLE_PI
            PiComm_SendMode(4);
            PiComm_SendTargetNum(target_N);
            PiComm_SendTargetDist(target_D);
#endif
#ifdef ENABLE_WIRELESS
            g_dist_d = 0;
            Wireless_SendStart();
#endif
            break;

        default:
            break;
    }
}

#ifdef ENABLE_PI
/* ===================== 处理树莓派测量数据 ===================== */
static void App_HandlePiData(void)
{
    /* 处理多正方形待选列表（模式3：ADV_2，显示所有编号供用户参考） */
    if (g_multi_updated) {
        g_multi_updated = 0;
        g_dist_d = g_multi_squares.distance;
        g_param_updated = 1;
        /* N控件已在PiComm_Process里更新，这里只更新距离 */
        char buf[16];
        sprintf(buf, "%05.1fcm", g_dist_d);
        HMI_SetText("D", buf);
    }

    /* 处理单条测量结果（模式2/4/5） */
    if (!PiComm_HasNewData()) return;

    const PiData_t *data = PiComm_GetData();
    if (!data->valid) return;

    g_dist_d     = data->distance;
    g_width_x    = data->size_cm;
    g_square_num = data->square_num;

    /* 更新HMI串口屏 */
    UI_UpdateResult(g_dist_d, g_width_x);

#ifdef ENABLE_WIRELESS
    /* 监控模式：当实时距离 ≤ 目标距离+5cm 时，发停车指令给小车
     * 需要连续2次满足条件才触发，避免单次测距异常误停 */
    {
        static uint8_t s_stop_sent = 0;
        //static uint8_t s_stop_confirm = 0;  // 连续确认计数
        if (!s_stop_sent && target_D > 0 && g_dist_d > 0 &&
            (currentState == STATE_BASIC_34 || currentState == STATE_ADV_2 ||
             currentState == STATE_ADV_3 || currentState == STATE_ADV_45))
        {
            if (g_dist_d <= (float)target_D + 30.0f) {
//                s_stop_confirm++;
//               if (s_stop_confirm >= 3) {
                    /* 连续发3次停车指令，确保小车收到 */
                    Wireless_SendStop();
                    Wireless_SendStop();
                    Wireless_SendStop();
                    s_stop_sent = 1;
                    g_wireless_feedback_count++;
                    
                    
                    Delay_ms(500);
                    switch (currentState) {
                        case STATE_BASIC_34: PiComm_SendMode(2); break;
                        case STATE_ADV_2:    PiComm_SendMode(3); break;
                        case STATE_ADV_3:    PiComm_SendMode(4); break;
                        case STATE_ADV_45:   PiComm_SendMode(5); break;
                        default: break;
                    }
//                    Delay_ms(100);
                    PiComm_SendTrigger();
////                }
            } 
//            else {
//                s_stop_confirm = 0;  // 不满足条件，重置计数
//            }
        }
        if (currentState == STATE_IDLE) { s_stop_sent = 0;}
    }
#endif
}
#endif

#ifdef ENABLE_WIRELESS
/* ===================== 处理无线反馈 ===================== */
static void App_HandleWireless(void)
{
    uint8_t feedback_data[2];
    uint8_t feedback_len = 0;

    if (!Wireless_ReceiveFeedback(feedback_data, &feedback_len)) return;

    if (feedback_data[0] == 0x01) {
        g_wireless_feedback_count++;
        Delay_ms(200);
#ifdef ENABLE_PI
        /* 小车到达，切换到对应测量模式再触发 */
        switch (currentState) {
            case STATE_BASIC_34:
                PiComm_SendMode(2);
                Delay_ms(100);
                PiComm_SendTrigger();
                break;
            case STATE_ADV_2:
                PiComm_SendMode(3);
                Delay_ms(100);
                PiComm_SendTrigger();
                break;
            case STATE_ADV_3:
                PiComm_SendMode(4);
                Delay_ms(100);
                PiComm_SendTrigger();
                break;
            case STATE_ADV_45:
                PiComm_SendMode(5);
                Delay_ms(100);
                PiComm_SendTrigger();
                break;
            default:
                PiComm_SendTrigger();
                break;
        }
#endif
        OLED_ShowString(4, 1, "Car Stopped!    ");
    }
}
#endif
