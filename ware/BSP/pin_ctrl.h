#include "stm32f4xx.h"                  // Device header
#ifndef __PIN_CTRL_H__
#define __PIN_CTRL_H__

void Pin_Ctrl_Init(void);
void Is_Battery_Charging(void);
void Power_Maintain_Ctrl(uint8_t status);
void Headphone_Power_Ctrl(uint8_t status);
void Speaker_Power_Ctrl(uint8_t status);
void USB_Power_Out_Ctrl(uint8_t status);
void USB_Slave_Host_Ctrl(uint8_t status);
void I2S_Exchange_Ctrl(uint8_t status);

#endif