#ifndef __VARIABLES_H__
#define __VARIABLES_H__

extern volatile uint8_t g_charge_status; // 0: 未充电, 1: 充电中, 2: 充电完成
extern volatile uint8_t g_vbus_status;    // 0: usb充电未连接, 1: 已连接 (usb不向外供电时有效 0-低电平 1-高电平)
extern volatile uint8_t g_headphone_status; // 0: 耳机未插入, 1: 耳机已插入
extern volatile uint8_t g_TFcard_status; // 0: TF卡未插入, 1: TF卡已插入

extern volatile uint8_t g_key_WKP_RT; // 0: 唤醒按键未按下, 1: 唤醒按键已按下
extern volatile uint8_t g_key_L_M_RT; // 0: 左摇杆未在中间位置, 1: 左摇杆在中间位置
extern volatile uint8_t g_key_R_M_RT; // 0: 右摇杆未在中间位置, 1: 右摇杆在中间位置

extern RTC_DateTypeDef now_date; // 当前日期 (RTC)
extern RTC_TimeTypeDef now_time; // 当前时间 (RTC)

extern volatile uint8_t g_pwm_inited; // PWM是否初始化完成标志 0：未初始化，1：已初始化

extern volatile uint8_t g_max98357_inited; // MAX98357A（喇叭）是否初始化标志 0：未初始化，1：已初始化
#endif // __VARIABLES_H__