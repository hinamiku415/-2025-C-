#include "stm32f10x.h"                  // Device header
#include <math.h>
#include "sys.h"

// PWM???
int PWM_L, PWM_R; 

int Target_turn = 0;
float Location_Out = 0;
float Target_Speed = 0;

// ?????????PID????  Kp Ki Kd,???????????????
PID_Init LocationPID = {20, 0, 0, 0, 0, 0, 0, 0, 0, 3000, 3000};   // ????
PID_Init SpeedPID = {300, 3, 0, 0, 0, 0, 0, 0, 0, 2500, 2500};	   // ????
PID_Init AnglePID = {150, 0, 10, 0, 0, 0, 0, 0, 0, 3000, 3000};    // ????
PID_Init LocationPID_D = {-20, 0, 0, 0, 0, 0, 0, 0, 0, 3000, 3000};    // ????

// PID??????
float PID_Calculate(PID_Init *pid, float actual) 
{
    // ????????
    pid->error = pid->Target - actual;
    
    // ??????
    if (pid == &AnglePID) 
	{
        if (pid->error > 180) pid->error -= 360;
        if (pid->error < -180) pid->error += 360;
    }

    
    // ?????????
    pid->integral += pid->error;
    if (pid->integral > pid->integral_limit) pid->integral = pid->integral_limit;
    else if (pid->integral < -pid->integral_limit) pid->integral = -pid->integral_limit;
    
    // ????????
    pid->derivative = pid->error - pid->last_error;
    
    // PID???????
    pid->output = pid->Kp * pid->error + 
                 pid->Ki * pid->integral + 
                 pid->Kd * pid->derivative;
    
    // ??????
    if (pid->output > pid->output_limit) pid->output = pid->output_limit;
    else if (pid->output < -pid->output_limit) pid->output = -pid->output_limit;
    
    // ???��??????��???
    pid->last_error = pid->error;
    
    return pid->output;
}

void Target_Location_arrive(float Target_Location)
{
	LocationPID.Target = Target_Location;
	Location_Out = PID_Calculate(&LocationPID, Actual_Location);

	//Target_Speed = 20;
	//SpeedPID.Target = Target_Speed;
	SpeedPID.Target = Location_Out;
    float Speed_Output = PID_Calculate(&SpeedPID,(Speed_L + Speed_R) / 2);
	
	AnglePID.Target = Target_turn;
	//PID_Calculate(&AnglePID,Yaw);
	PID_Calculate(&AnglePID,Track_Target_turn);
	
	//PWM_L = Speed_Output;
    //PWM_R = Speed_Output;
	
    PWM_L = Speed_Output + AnglePID.output;  // ????PWM
    PWM_R = Speed_Output - AnglePID.output;  // ????PWM
	
    if (PWM_L > 2160) PWM_L = 2160;
    else if (PWM_L < -2160) PWM_L = -2160;
    if (PWM_R > 2160) PWM_R = 2160;
    else if (PWM_R < -2160) PWM_R = -2160;
    
	if(Car_state == 0)
	{
		SetPWM(0,0);
		return;
	}
	else
	{
		SetPWM(PWM_L,PWM_R);
	}
}

//void Target_Location_arrive_D(float Target_Location,float Actual_Location)
//{

//	LocationPID_D.Target = Target_Location;
//	LocationPID_D.output = PID_Calculate(&LocationPID_D, Actual_Location);
//	
//    PWM_L = LocationPID_D.output + AnglePID.output;  // ????PWM
//    PWM_R = LocationPID_D.output - AnglePID.output;  // ????PWM
//	
//    if (PWM_L > 2160) PWM_L = 2160;
//    else if (PWM_L < -2160) PWM_L = -2160;
//    if (PWM_R > 2160) PWM_R = 2160;
//    else if (PWM_R < -2160) PWM_R = -2160;
//    
//	if(Car_state == 0)
//	{
//		SetPWM(0,0);
//		return;
//	}
//	else
//	{
//		SetPWM(PWM_L,PWM_R);
//	}
//}

void Target_Location_arrive_D(float Target_Speed_D)
{

	SpeedPID.Target = Target_Speed_D;
	PID_Calculate(&SpeedPID,(Speed_L + Speed_R) / 2);
	
    AnglePID.Target = Target_turn;
	PID_Calculate(&AnglePID,Track_Target_turn);
    
    PWM_L = SpeedPID.output + AnglePID.output;
    PWM_R = SpeedPID.output - AnglePID.output;
	
    if (PWM_L > 4000) PWM_L = 4000;
    else if (PWM_L < -4000) PWM_L = -4000;
    if (PWM_R > 4000) PWM_R = 4000;
    else if (PWM_R < -4000) PWM_R = -4000;
    
	if(Car_state2 == 0)
	{
		SetPWM(0,0);
		return;
	}
	else
	{
		SetPWM(PWM_L,PWM_R);
	}
}