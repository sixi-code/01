#include "stm32f4xx.h"                  // Device header

#ifndef __LCD_PWM_H__
#define __LCD_PWM_H__

void LCD_TIM8_PWM_Init(void);
void LCD_PWM_DeInit(void);

//duty:0~256 实际最大值280
void LCD_PWM_SetDutyCycle(uint16_t duty);
//frequency:1~60k
void LCD_PWM_SetFrequency(uint16_t frequency);

#endif
