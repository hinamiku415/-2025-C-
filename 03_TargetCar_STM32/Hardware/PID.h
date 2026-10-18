#ifndef __PID_H
#define __PID_H

typedef struct {
    float Kp;                  // 比例系数
    float Ki;                  // 积分系数
    float Kd;                  // 微分系数
    float Target;            // 目标值
    float error;               // 当前误差
    float last_error;          // 上一次误差
    float integral;            // 积分项
    float derivative;          // 微分项
    float output;              // 输出值
    float integral_limit;      // 积分限幅
    float output_limit;        // 输出限幅
} PID_Init;

float PID_Calculate(PID_Init *pid, float actual);
void Target_Location_arrive(float Target_Location);
//void Target_Location_arrive_D(float Target_Location,float Actual_Location);
void Target_Location_arrive_D(float Target_Speed_D);

#endif
