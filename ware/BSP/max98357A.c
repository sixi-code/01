// MAX98357A（喇叭）初始化
#include "stm32f4xx.h"
#include "variables.h"
#include "pin_ctrl.h"

void MAX98357_Init(void)
{	//初始化MAX98357A（喇叭）供电
	Speaker_Power_Ctrl(1);
	g_max98357_inited = 1;
}

void MAX98357_Deinit(void)
{	//关闭MAX98357A（喇叭）供电
	Speaker_Power_Ctrl(0);
	g_max98357_inited = 0;
}
