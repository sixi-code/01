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
//   g_charge_status = 1 → BAT IN低电平, 正在充电
//   g_charge_status = 2 → VBUS高电平且BAT IN高电平, 充电完成
//   g_charge_status = 0 → VBUS低电平, 未充电
void Is_Battery_Charging(void){
    uint8_t Bat_Status = GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_8);
    g_vbus_status = GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_13);
    if(!Bat_Status) g_charge_status = 1;
    else {
        if(g_vbus_status) g_charge_status = 2;
        else g_charge_status = 0;
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
//   status: 0 → 输出低电平, 允许断电
//   status: 1 → 输出高电平, 保持供电
void Power_Maintain_Ctrl(uint8_t status){
    if(status == 0) GPIO_ResetBits(GPIOA, GPIO_Pin_11);
    else GPIO_SetBits(GPIOA, GPIO_Pin_11);

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
//   status: 0 → 输出低电平, 关闭音响电源
//   status: 1 → 输出高电平, 打开音响电源
void Speaker_Power_Ctrl(uint8_t status){
    if(status == 0) GPIO_ResetBits(GPIOC, GPIO_Pin_7);
    else GPIO_SetBits(GPIOC, GPIO_Pin_7);

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
//   status: 0 → 输出低电平, 关闭耳机电源
//   status: 1 → 输出高电平, 打开耳机电源
void Headphone_Power_Ctrl(uint8_t status){
    if(status == 0) GPIO_ResetBits(GPIOA, GPIO_Pin_9);
    else GPIO_SetBits(GPIOA, GPIO_Pin_9);

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
//   PC5高电平 → 耳机已连接
//   PC5低电平 → 耳机未连接
void Is_Headphone_Connecting(void){
    g_headphone_status = GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_5);
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
//   PB8低电平 → TF卡已连接
//   PB8高电平 → TF卡未连接
void Is_TFcard_Connecting(void){
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
//   status: 0 → 输出低电平, 选择耳机输出
//   status: 1 → 输出高电平, 选择喇叭输出
// 注: 持久化待实现
void I2S_Exchange_Ctrl(uint8_t status){
    if(status == 0) GPIO_ResetBits(GPIOB, GPIO_Pin_11);
	else GPIO_SetBits(GPIOB, GPIO_Pin_11);
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
//   status: 0 → 关闭对外供电
//   status: 1 → 打开对外供电
void USB_Power_Out_Ctrl(uint8_t status){
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
//   status: 0 → 输出低电平, 切换为设备模式(Slave)
//   status: 1 → 输出高电平, 切换为主机模式(Host)
void USB_Slave_Host_Ctrl(uint8_t status){
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