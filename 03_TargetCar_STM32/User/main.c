#include "stm32f10x.h"                  // Device header
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "sys.h"

/*全局变量*/
uint8_t KeyScan; //摁键摁下数据
uint8_t KeyNum;  //记录摁键数值
uint8_t Car_state = 0;
uint8_t Car_state2 = 0;
uint8_t Target_Location = 0;
float Actual_Location_D = 0;


uint8_t Buzzer_Flag = 0;
uint16_t Time = 0;
uint16_t Time2 = 0;

void Control_kick(void);

int main(void)
{
	/*模块初始化*/
	SYS_Init();
	
	while (1)
	{
		//获取摁键摁下
		KeyScan = Key_GetNum();
		if(KeyScan == 1)
		{
			// 基础1：按KEY1启动编码器跑100cm，到达后自动停车
			if(Car_state == 0) {
				Actual_Location = 0.0f;
				Target_Location = 100;
				Car_state = 1;
			}
		}
		if(KeyScan == 2)
		{
			Car_state2 = !Car_state2;
		}
		
		static uint16_t Buzzer_Time = 0;
		if(Buzzer_Flag == 1)
		{
			Buzzer_ON();
			Buzzer_Time++;
			if(Buzzer_Time >= 20)
			{
				Buzzer_OFF();
				Buzzer_Flag = 0;
				Buzzer_Time = 0;
			}
		}

		
		/* 蓝牙接收（JDY-31，UART4，PC10/PC11） */
		if (uart4_get_flag())
		{
			char *cmd = (char*)uart4_get_buf();
			
			/* OLED 显示收到的原始指令 */
			OLED_Printf(0, 15, OLED_8X16, "BT:%s      ", cmd);
			
			if (strcmp(cmd, "GO") == 0)
			{
				Car_state2 = 1;
			}
			else if (strcmp(cmd, "STOP") == 0)
			{
				Car_state2 = 0;
				SetPWM(-800, -800);
				delay_ms(200);
				SetPWM(0, 0);
				Buzzer_Flag = 1;
				// 不发反馈，树莓派通过测距判断小车是否停稳
			}
			else if (strncmp(cmd, "TARGET:", 7) == 0)
			{
				Actual_Location = 0.0f;
				Target_Location = (uint8_t)atoi(cmd + 7);
				OLED_Printf(0, 30, OLED_8X16, "Target: %d  ", Target_Location);
			}
			uart4_clear_buf();
		}

		
		//变量显示区//
		//*********************************************陀螺仪打印区
		//OLED_Printf(0,0,OLED_8X16,"Pitch: %.1f",Pitch);
		//OLED_Printf(0,15,OLED_8X16,"Roll: %.1f",Roll);
		//OLED_Printf(0,30,OLED_8X16,"Yaw: %.1f",Yaw);
		
		//*********************************************编码器打印区
		//OLED_Printf(0,30,OLED_8X16,"L:%d",Speed_L);
		//OLED_Printf(0,45,OLED_8X16,"R:%d",Speed_R);+
		OLED_Printf(0,0,OLED_8X16,"Location:%.1f",Actual_Location);
		
		//*********************************************循迹数据打印区
		//OLED_ShowBinNum(0,0,Track,8,OLED_8X16);
		
		//*********************************************标志位打印区
		if(Car_state == 1)
		{
			OLED_Printf(0,45,OLED_8X16,"State:%d",Car_state);
		}
		if(Car_state2 == 1)
		{
			OLED_Printf(0,45,OLED_8X16,"State2:%d",Car_state2);
		}

		OLED_Update();
		
		// 串口打印已禁用，避免阻塞主循环影响无线接收
		// printf("Roll:%.1f Pitch:%.1f Yaw:%.1f\r\n",Roll,Pitch,Yaw);
		// printf("Ax:%.2f Ay:%.2f Az:%.2f\r\n\r\n",Ax,Ay,Az);
		
		delay_ms(10);  // 缩短到10ms，主循环约20~30ms一圈
	}
}

//10ms中断一次
void TIM1_UP_IRQHandler(void)
{
	if (TIM_GetITStatus(TIM1, TIM_IT_Update) == SET)
	{
		Key_Tick();
		Control_kick();
		Encoder_Read();
		
		Read_Track_DATA(&Track);
		Track_Control();
		
		//停车flag（基础1编码器到达）
		if(Car_state == 2)
		{
			Buzzer_Flag = 1;
			Car_state = 0;
		}
		
		if(Car_state2 == 2)
		{
			Buzzer_Flag = 1;
			Car_state2 = 0;
		}
		
		if(Car_state2 == 1) Target_Location_arrive_D(7);
		if(Car_state == 1) Target_Location_arrive(Target_Location);
		if(Car_state == 0 && Car_state2 == 0) SetPWM(0,0);
		
		TIM_ClearITPendingBit(TIM1, TIM_IT_Update);//清除标志位
	}
}

void Control_kick(void)		
{	
	if(Car_state == 1)
	{
		if(fabs(Actual_Location - Target_Location) <= 0.3f)
		{
			if(Time == 0)
			{
				Time = 50;  //10 * 50 == 500ms
			}
		}
		else
		{
			Time = 0;
		}

		if(Time > 0)
		{
			Time--;
			if(Time == 0)
			{
				Car_state = 2;
			}
		}
	}
	
	if(Car_state2 == 1)
	{
		// Car_state2 由发射端停车指令控制，不做自动到达检测
	}
}



