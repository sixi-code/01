#include "stm32f4xx.h"                  // Device header

#ifndef __ADC_H__
#define __ADC_H__
	
void ADC1_DMA_Init(void);
void ADC_StartConversion(void);
void ADC_Calculate_Voltage(void);

#endif
