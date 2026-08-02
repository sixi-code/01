#include "stm32f4xx.h"


void Pin_Ctrl_Init(void){
    
    Battery_Ischarging_Pin_Init();
	Power_Maintain_Pin_Init();
	Speaker_Power_Pin_Init();
	Headphone_Power_Pin_Init();
	Headphone_Isconnecting_Pin_Init(); 
	TFcard_Isconnecting_Pin_Init(); 
	I2S_Exchange_Pin_Init(); 
	USB_Power_Out_Pin_Init(); 
	USB_Slave_Host_Pin_Init(); 

}

void Battery_Ischarging_Pin_Init(void){
    
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

}

void Is_Battery_Charging(void){
    
    uint8_t Bat_Status = GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_8);
    g_vbus_status = GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_13);
    if(!Bat_Status) g_charge_status = 1;
    else {
        if(g_vbus_status) g_charge_status = 2;
        else g_charge_status = 0;
    }

}

void Power_Maintain_Pin_Init(void){
    
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

}