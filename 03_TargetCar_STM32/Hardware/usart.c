#include "usart.h"	  

float Ax, Ay, Az;    // 加速度
float Wx, Wy, Wz;    // 角速度
float Roll, Pitch, Yaw;  // 角度


//加入以下代码,支持printf函数,而不需要选择use MicroLIB
#if 1
#pragma import(__use_no_semihosting)             
//标准库需要的支持函数                 
struct __FILE 
{ 
	int handle; 

}; 

FILE __stdout;       
//定义_sys_exit()以避免使用半主机模式    
void _sys_exit(int x) 
{ 
	x = x; 
} 
//重定义fputc函数 
int fputc(int ch, FILE *f)
{      
	while((USART1->SR&0X40)==0);//循环发送,直到发送完毕   
    USART1->DR = (u8) ch;      
	return ch;
}
#endif 

u8 USART_RX_BUF[64];     //接收缓冲,最大64个字节.
//接收状态
//bit7，接收完成标志
//bit6，接收到0x0d
//bit5~0，接收到的有效字节数目
u8 USART_RX_STA=0;       //接收状态标记

void uart1_init(u32 bound)
{
	//GPIO端口设置
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1|RCC_APB2Periph_GPIOA|RCC_APB2Periph_AFIO, ENABLE);
	//USART1_TX   PA.9
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	//USART1_RX	  PA.10
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	GPIO_Init(GPIOA, &GPIO_InitStructure);  
	//USART 初始化设置
	USART_InitStructure.USART_BaudRate = bound;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
	USART_Init(USART1, &USART_InitStructure);
//	USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);//开启中断
	USART_Cmd(USART1, ENABLE);                    //使能串口 
}

void uart3_init(u32 bound)
{
	//GPIO端口设置
	GPIO_InitTypeDef  GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef  NVIC_InitStructure;
	
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE); //串口时钟使能	
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);  //GPIO使用使能

	//UARTx TX
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;	//复用推挽输出
	GPIO_Init(GPIOB, &GPIO_InitStructure);

	//UARTx RX
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;//浮空输入
	GPIO_Init(GPIOB, &GPIO_InitStructure);

	//Usart2 NVIC 配置
	NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=3 ;//抢占优先级3
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;		//子优先级3
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	//USART 初始化设置
	USART_InitStructure.USART_BaudRate = bound;//串口波特率
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;//字长为8位数据格式
	USART_InitStructure.USART_StopBits = USART_StopBits_1;//一个停止位
	USART_InitStructure.USART_Parity = USART_Parity_No;//无奇偶校验位
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;//无硬件数据流控制
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;	//收发模式
	USART_Init(USART3, &USART_InitStructure);
	
	
    USART_ITConfig(USART3, USART_IT_RXNE , ENABLE);//串口接收中断
    USART_Cmd(USART3, ENABLE);
}


// JY61P 数据解析函数
void JY61P_Decode(uint8_t Data)
{
    // 校验和验证
	static u8 RX_Data[250];
	static u8 len = 0;
	RX_Data[len++]=Data; //将收到的数据放到缓冲区
	
	if(len < 11) return; //数据没满重传
	
	u8 sum = 0;
    for(u8 i=0; i<10; i++) sum += RX_Data[i];
    if(sum != RX_Data[10])  // 校验失败，丢弃
    {
        len = 0;
        return;
    }
	
	
		switch(RX_Data[1])
		{
        case 0x51:  // 加速度
            Ax = (short)((short)RX_Data[3]<<8 | RX_Data[2])/32768.0*16.0;
            Ay = (short)((short)RX_Data[5]<<8 | RX_Data[4])/32768.0*16.0;
            Az = (short)((short)RX_Data[7]<<8 | RX_Data[6])/32768.0*16.0;
            break;

        case 0x52:  // 角速度
            Wx = (short)((short)RX_Data[3]<<8 | RX_Data[2])/32768.0*2000.0;
            Wy = (short)((short)RX_Data[5]<<8 | RX_Data[4])/32768.0*2000.0;
            Wz = (short)((short)RX_Data[7]<<8 | RX_Data[6])/32768.0*2000.0;
            break;

        case 0x53:  // 角度
            Roll =  (short)((short)RX_Data[3]<<8 | RX_Data[2])/32768.0*180.0;
            Pitch = (short)((short)RX_Data[5]<<8 | RX_Data[4])/32768.0*180.0;
            Yaw =   (short)((short)RX_Data[7]<<8 | RX_Data[6])/32768.0*180.0;
            break;
    }
	len = 0; //清理缓存
	
}

// 串口2 接收中断服务函数
void USART3_IRQHandler(void)
{
    if(USART_GetITStatus(USART3, USART_IT_RXNE) == SET)
    {
		uint8_t Data = (USART3 -> DR & (uint8_t)0x01FF);
        JY61P_Decode(Data);
    }
	USART_ClearITPendingBit(USART3, USART_IT_RXNE);
}

/* ===================== UART4：JDY-31 蓝牙模块（PC10=TX, PC11=RX）===================== */

#define BT_RX_BUF_SIZE  64
static uint8_t  bt_rx_buf[BT_RX_BUF_SIZE];
static uint8_t  bt_rx_len = 0;
volatile uint8_t bt_rx_flag = 0;   // 收到完整一行（\n结尾）

void uart4_init(u32 bound)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_UART4, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);

    /* PC10: TX - 复用推挽输出 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    /* PC11: RX - 浮空输入 */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate            = bound;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(UART4, &USART_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel                   = UART4_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 2;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    USART_ITConfig(UART4, USART_IT_RXNE, ENABLE);
    USART_Cmd(UART4, ENABLE);
}

/* 发送一个字节 */
void uart4_send_byte(uint8_t byte)
{
    USART_SendData(UART4, byte);
    while (USART_GetFlagStatus(UART4, USART_FLAG_TXE) == RESET);
}

/* 发送字符串 */
void uart4_send_string(char *str)
{
    while (*str) uart4_send_byte(*str++);
}

/* 获取接收标志（收到完整一行） */
uint8_t uart4_get_flag(void)
{
    if (bt_rx_flag) { bt_rx_flag = 0; return 1; }
    return 0;
}

/* 获取接收缓冲区 */
uint8_t* uart4_get_buf(void)
{
    return bt_rx_buf;
}

/* 清空接收缓冲区 */
void uart4_clear_buf(void)
{
    bt_rx_len = 0;
    bt_rx_buf[0] = '\0';
}

/* UART4 中断：收到\n时置flag */
void UART4_IRQHandler(void)
{
    if (USART_GetITStatus(UART4, USART_IT_RXNE) == SET)
    {
        uint8_t data = (uint8_t)(UART4->DR & 0xFF);
        if (data == '\n' || data == '\r') {
            if (bt_rx_len > 0) {
                bt_rx_buf[bt_rx_len] = '\0';
                bt_rx_flag = 1;
                bt_rx_len = 0;
            }
        } else {
            if (bt_rx_len < BT_RX_BUF_SIZE - 1)
                bt_rx_buf[bt_rx_len++] = data;
        }
        USART_ClearITPendingBit(UART4, USART_IT_RXNE);
    }
}
