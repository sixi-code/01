#include "stm32f4xx.h"                  // Device header


// IWTG初始化
void IWTG_Init(void)
{   
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);// 允许写入IWDG寄存器
    IWDG_SetPrescaler(IWDG_Prescaler_128);// 设置IWDG预分频器为128
    IWDG_SetReload(938);// 设置IWDG重装载值为938，计算公式：重装载值 = (IWDG计数器时钟频率 / 预分频器) * 超时时间 - 1
    IWDG_ReloadCounter();// 重新加载IWDG计数器
    IWDG_Enable();// 使能IWDG 喂狗
}


// 喂狗函数：在主循环中定期调用
void IWDG_Feed(void)
{
    IWDG_ReloadCounter();
}