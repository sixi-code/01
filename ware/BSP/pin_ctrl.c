//电源控制引脚初始化
#include "stm32f4xx.h"
#include "variables.h"


// 充电检测引脚初始化
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

// 检测电池充电状态
void Is_Battery_Charging(void){
    uint8_t Bat_Status = GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_8);
    g_vbus_status = GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_13);
    if(!Bat_Status) g_charge_status = 1;// BAT IN为低电平表示正在充电
    else {
        if(g_vbus_status) g_charge_status = 2;// VBUS高电平且BAT IN高电平表示充电完成
        else g_charge_status = 0;// VBUS低电平表示未充电
    }

}

// 电源维持引脚初始化
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

// 控制电源维持状态
void Power_Maintain_Ctrl(uint8_t status){
    // status: 0: 允许断电, 1: 保持供电
    if(status == 0) GPIO_ResetBits(GPIOA, GPIO_Pin_11);// 输出低电平，允许断电
    else GPIO_SetBits(GPIOA, GPIO_Pin_11);// 输出高电平，保持供电

}

// 音响供电引脚初始化
void Speaker_Power_Pin_Init(void){
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

}

// 控制音响供电状态
void Speaker_Power_Ctrl(uint8_t status){
    // status: 0: 关闭音响电源, 1: 打开音响电源
    if(status == 0) GPIO_ResetBits(GPIOC, GPIO_Pin_7);// 输出低电平，关闭电源
    else GPIO_SetBits(GPIOC, GPIO_Pin_7);// 输出高电平，打开电源

}

// 耳机供电引脚初始化
void Headphone_Power_Pin_Init(void){
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

}

// 控制耳机供电状态
void Headphone_Power_Ctrl(uint8_t status){
    // status: 0: 关闭耳机电源, 1: 打开耳机电源
    if(status == 0) GPIO_ResetBits(GPIOA, GPIO_Pin_9);// 输出低电平，关闭电源
    else GPIO_SetBits(GPIOA, GPIO_Pin_9);// 输出高电平，打开电源

}

// 耳机连接检测引脚初始化
void Headphone_Isconnecting_Pin_Init(void){
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

}

// 检测耳机连接状态
void Is_Headphone_Connecting(void){
    //PC5高电平已连接，低电平未连接
    g_headphone_status = GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_5);// 读取PC5状态
}

// TF卡连接检测引脚初始化
void TFcard_Isconnecting_Pin_Init(void){
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

}
// 检测TF卡连接状态
void Is_TFcard_Connecting(void){
    //PB8低电平已连接，PB8高电平未连接
    g_TFcard_status = GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_8) ? 0 : 1;
}

// I2S音频切换引脚初始化
void I2S_Exchange_Pin_Init(void){
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

}

// 控制I2S音频切换状态
void I2S_Exchange_Ctrl(uint8_t status){
    // status: 0: 选择耳机输出, 1: 选择喇叭输出
    if(status == 0) GPIO_ResetBits(GPIOB, GPIO_Pin_11);       // 输出低电平，选择耳机
	else GPIO_SetBits(GPIOB, GPIO_Pin_11);                   // 输出高电平，选择喇叭
    //持久化待实现
}

// USB向外供电引脚初始化
void USB_Power_Out_Pin_Init(void){
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

}

// 控制USB向外供电状态
void USB_Power_Out_Ctrl(uint8_t status){
    // status: 0: 关闭供电, 1: 打开供电
    if(status == 0) GPIO_ResetBits(GPIOA, GPIO_Pin_12);
    else GPIO_SetBits(GPIOA, GPIO_Pin_12);

}

// USB主从切换引脚初始化
void USB_Slave_Host_Pin_Init(void){
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

}

// 控制USB主从切换状态
void USB_Slave_Host_Ctrl(uint8_t status){
    // status: 0: 切换为从机模式, 1: 切换为主机模式
    if(status == 0) GPIO_ResetBits(GPIOA, GPIO_Pin_10);
    else GPIO_SetBits(GPIOA, GPIO_Pin_10);

}

// 初始化所有引脚
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