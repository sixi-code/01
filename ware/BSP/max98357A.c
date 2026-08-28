// MAX98357A（喇叭）初始化
#include "stm32f4xx.h"
#include "variables.h"
#include "pin_ctrl.h"

// 初始化MAX98357A（喇叭）供电
void MAX98357_Init(void)
{
	Speaker_Power_Ctrl(1);
	g_max98357_inited = 1;
}

// 关闭MAX98357A（喇叭）供电
void MAX98357_Deinit(void)
{
	Speaker_Power_Ctrl(0);
	g_max98357_inited = 0;
}