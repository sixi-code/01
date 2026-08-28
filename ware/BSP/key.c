//摇杆按下位置初始化，检测，唤醒按键初始化，进入待机模式，读取唤醒按键状态
#include "stm32f4xx.h"
#include "pin_ctrl.h"
#include "variables.h"

// 初始化唤醒按键
void Wakeup_Key_Init(void) 
{
    // 检查是否从Standby模式唤醒
    if (PWR_GetFlagStatus(PWR_FLAG_SB) != RESET) 
    {
        PWR_ClearFlag(PWR_FLAG_SB);
    }
    
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE); // 使能GPIOA时钟

    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN;        // 输入模式
    GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_DOWN;      // 下拉
    GPIO_Init(GPIOA, &GPIO_InitStruct);
}

// 进入待机模式
void Enter_Standby_Mode(void)
{
    Speaker_Power_Ctrl(0);
    Headphone_Power_Ctrl(0);

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);  // 使能PWR时钟
    PWR_WakeUpPinCmd(ENABLE); // 使能PA0的WK
    PWR_ClearFlag(PWR_FLAG_WU); // 清除唤醒标志（避免误触发）
    PWR_EnterSTANDBYMode(); // 进入Standby模式
}

// 读取唤醒按键状态
void Read_Wakeup_Key(void)
{
    g_key_WKP_RT = GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0);
}
 
// 初始化摇杆中间位置检测
void Joystick_Middle_Init(void) 
{
    GPIO_InitTypeDef GPIO_InitStructure;
    //PB2 左摇杆中间位置检测, PC2 右摇杆中间位置检测
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);
    
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    GPIO_Init(GPIOC, &GPIO_InitStructure);
}

// 获取摇杆中间位置状态
void Get_Joystick_Middle(void) 
{
    g_key_L_M_RT = GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_2) ? 0 : 1; // 左摇杆中间位置检测
    g_key_R_M_RT = GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_2) ? 0 : 1; // 右摇杆中间位置检测
}