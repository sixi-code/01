#include "stm32f4xx.h"

volatile uint8_t g_charge_status = 0; // 0: 未充电, 1: 充电中, 2: 充电完成
volatile uint8_t g_vbus_status = 0;    // 0: usb充电未连接, 1: 已连接 (usb不向外供电时有效 0-低电平 1-高电平)
volatile uint8_t g_headphone_status = 0; // 0: 耳机未插入, 1: 耳机已插入
volatile uint8_t g_TFcard_status = 0; // 0: TF卡未插入, 1: TF卡已插入