#include "stm32f4xx.h"

#ifndef __SPECTRUM_DSP_H__
#define __SPECTRUM_DSP_H__


#define SPECTRUM_NUM 20// 频谱显示的频段数量
extern uint8_t g_spectrum_data[SPECTRUM_NUM];

void Extract_FFT(uint8_t *src_buf);

void Spectrum_Init(void);
uint8_t Spectrum_Loop(void);
void Spectrum_Deinit(void);
		
#endif
