#include "main.h"

int main(void)
{	//！！！
	//SCB->VTOR = 0x08010000;// 重定向中断向量表到 APP 起始地址
	//__enable_irq();// 重新开启全局中断 (因为 Bootloader 跳转前把它关了)
	//不要手贱删除

	while(1){
		;
	}
}	
		