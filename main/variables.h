#ifndef __VARIABLES_H__
#define __VARIABLES_H__

#include "lunar.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "event_groups.h"
#include "stm32f4xx.h"
// 全局变量声明
// FreeRTOS相关
extern SemaphoreHandle_t xFlashMutex; // w25q128互斥锁
extern SemaphoreHandle_t xFlashSemaphore; // w25q128计数型信号量
extern SemaphoreHandle_t xI2SSemaphore; // music dma 传输完成信号量
extern EventGroupHandle_t xLcdEventGroup; // lcd事件组
extern SemaphoreHandle_t xIICMutex; // iic互斥锁
extern SemaphoreHandle_t xSDcardMutex; // sdcard互斥锁
extern SemaphoreHandle_t xSDcardSemaphore; // sdcard计数型信号量
// pin_ctrl.c
extern volatile uint8_t g_charge_status; // 0: 未充电, 1: 充电中, 2: 充电完成
extern volatile uint8_t g_vbus_status;    // 0: usb充电未连接, 1: 已连接 (usb不向外供电时有效 0-低电平 1-高电平)
extern volatile uint8_t g_headphone_status; // 0: 耳机未插入, 1: 耳机已插入
extern volatile uint8_t g_TFcard_status; // 0: TF卡未插入, 1: TF卡已插入
// key.c
extern volatile uint8_t g_key_WKP_RT; // 0: 唤醒按键未按下, 1: 唤醒按键已按下
extern volatile uint8_t g_key_L_M_RT; // 0: 左摇杆未在中间位置, 1: 左摇杆在中间位置
extern volatile uint8_t g_key_R_M_RT; // 0: 右摇杆未在中间位置, 1: 右摇杆在中间位置
// rng.c
extern RTC_DateTypeDef now_date; // 当前日期 (RTC)
extern RTC_TimeTypeDef now_time; // 当前时间 (RTC)
//lcd_pwm.c
extern volatile uint8_t g_pwm_inited; // PWM是否初始化完成标志 0：未初始化，1：已初始化
//max98357.c
extern volatile uint8_t g_max98357_inited; // MAX98357A（喇叭）是否初始化标志 0：未初始化，1：已初始化
//adc.c
extern volatile uint8_t g_adc_dma_finished; // ADC DMA传输完成标志
extern volatile uint16_t g_slave_cc1_value; // Type-C Slave CC1电压值 (ADC采样值)
extern volatile uint16_t g_slave_cc2_value; // Type-C Slave CC2电压(ADC采样值)
extern volatile uint16_t g_host_cc1_value;  // Type-C Host CC1电压值 (ADC采样值)
extern volatile uint16_t g_host_cc2_value;  // Type-C Host CC2电压值 (ADC采样值)
extern volatile uint8_t g_usb_status; // Type-C状态 0: 未连接, 1: Slave, 2: Host, 3: AC, 4: CC_IDLE, 5: CC_OKEY, 6: AC_IDLE, 7: AC_OKEY
extern volatile float g_battery_voltage; // 电池电压 (单位: V)
extern volatile int16_t g_key_L_X; // 左摇杆 X 轴
extern volatile int16_t g_key_L_Y; // 左摇杆 Y 轴
extern volatile int16_t g_key_R_X; // 右摇杆 X 轴
extern volatile int16_t g_key_R_Y; // 右摇杆 Y 轴
//systick_conf.c
extern volatile uint32_t RTOS_OK; // FreeRTOS调度器状态 0：未启动，1：已启动
//rtc_clock.h
extern volatile uint8_t RTC_HFmt;  //0-24 1-12
extern volatile uint8_t RTC_Week;  //1-7
extern volatile uint8_t RTC_Year;  //0-99
extern volatile uint8_t RTC_Moth;  //1-12
extern volatile uint8_t RTC_Date;  //1-31
extern volatile uint8_t RTC_Hour;  //0-24
extern volatile uint8_t RTC_Mint;  //0-60
extern volatile uint8_t RTC_Secd;  //0-60

extern Lunar_t now_lunar; //农历

extern volatile uint8_t g_lcd_user; // 当前LCD使用者标识
// es9018k2m.c
extern volatile uint8_t g_es9018_inited;     // ES9018初始化标志
extern volatile uint8_t music_bitdepth;      // 音频位深 16/24/32
extern volatile uint8_t kv_hdp_value;        // 耳机音量 (0-255)
extern volatile uint8_t kv_es9018_volume;    // ES9018 DAC 音量缓存
extern volatile uint8_t g_TFcard_inited;     // TF卡初始化标志
#endif // __VARIABLES_H__