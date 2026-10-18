#ifndef __USART_H
#define __USART_H
#include "stdio.h"	
#include "sys.h" 


void uart1_init(u32 bound);
void uart3_init(u32 bound);
void uart4_init(u32 bound);         // UART4：JDY-31蓝牙（PC10=TX, PC11=RX）
void uart4_send_byte(uint8_t byte);
void uart4_send_string(char *str);
uint8_t  uart4_get_flag(void);
uint8_t* uart4_get_buf(void);
void     uart4_clear_buf(void);
extern volatile uint8_t bt_rx_flag;
void JY61P_Decode(uint8_t Data);
void Serial_SendByte(uint8_t Byte);
void Serial_SendArray(uint8_t *Array, uint16_t Length);
void Serial_SendString(char *String);
uint32_t Serial_Pow(uint32_t X, uint32_t Y);
void Serial_SendNumber(uint32_t Number, uint8_t Length);
void Serial_Printf(char *format, ...);


// 全局变量：存储解析后的陀螺仪数据
extern float Ax, Ay, Az;    // 加速度
extern float Wx, Wy, Wz;    // 角速度
extern float Roll, Pitch, Yaw;  // 角度


#endif


