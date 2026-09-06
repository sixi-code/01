#ifndef __FONTUPD_H__
#define __FONTUPD_H__	 
#include <stm32f4xx.h>

//字库信息结构体定义
//用来保存字库基本信息，地址，大小等
__packed typedef struct 
{
	uint8_t  fontok; //字库完好标志,0xAA表示完好,其他表示丢失		
	uint32_t unigbkaddr; // GBK字库地址
	uint32_t unigbksize; // GBK字库大小
	uint32_t font8addr;	// 8点阵字库地址
	uint32_t font8size;	// 8点阵字库大小
	uint32_t font12addr; // 12点阵字库地址
	uint32_t font12size; // 12点阵字库大小
	uint32_t font16addr; // 16点阵字库地址		
	uint32_t font16size; // 16点阵字库大小
	uint32_t font24addr; // 24点阵字库地址
	uint32_t font24size; // 24点阵字库大小	
}_font_info; 

extern _font_info ftinfo;	//字库信息结构体
extern uint8_t __g_font_buf[210];

uint8_t update_font(uint8_t* src);			        //更新全部字库
uint8_t font_init(void);					        //初始化字库

#endif





















