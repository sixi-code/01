#ifndef __VARIABLES_H__
#define __VARIABLES_H__

#include "lunar.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "event_groups.h"
#include "stm32f4xx.h"
#include "flashdb.h"
#include "es9018k2m.h"


//进出临界区保证原子性
#define GLOBAL(...)  do { \
    taskENTER_CRITICAL(); \
    __VA_ARGS__;          \
    taskEXIT_CRITICAL();  \
} while(0)
//GLOBAL(global_counter = 1);

// 全局变量声明
// FreeRTOS相关
extern SemaphoreHandle_t xFlashMutex; // w25q128互斥锁
extern SemaphoreHandle_t xFlashSemaphore; // w25q128计数型信号量
extern SemaphoreHandle_t xI2SSemaphore; // music dma 传输完成信号量
extern SemaphoreHandle_t xIICMutex; // iic互斥锁
extern SemaphoreHandle_t xSDcardMutex; // sdcard互斥锁
extern SemaphoreHandle_t xSDcardSemaphore; // sdcard计数型信号量
extern SemaphoreHandle_t xBSCMutex; // tlsf互斥锁
extern SemaphoreHandle_t xCCMMutex; // tlsf互斥锁
extern SemaphoreHandle_t xFDBSemaphore; // flashdb互斥锁
extern SemaphoreHandle_t xTaskManagerSemaphore; // taskmanager信号量
extern EventGroupHandle_t xLcdEventGroup; // lcd事件组

// FreeRTOS所有任务句柄
extern TaskHandle_t Basic_Task_handler;
extern TaskHandle_t Lvgl_Task_handler;
extern TaskHandle_t USB_Task_handler;
extern TaskHandle_t Music_Task_handler;
extern TaskHandle_t Media_Task_handler;
extern TaskHandle_t Game_Task_handler;
extern TaskHandle_t Font_Task_handler;
extern TaskHandle_t FileOp_Task_handler;
extern TaskHandle_t Start_Task_handler;
extern TaskHandle_t Task_Manager_handler;

// pin_ctrl.c
extern volatile uint8_t g_charge_status; // 0: 未充电, 1: 充电中, 2: 充电完成
extern volatile uint8_t g_vbus_status;    // 0: usb充电未连接, 1: 已连接 (usb不向外供电时有效 0-低电平 1-高电平)
extern volatile uint8_t g_headphone_status; // 0: 耳机未插入, 1: 耳机已插入
extern volatile uint8_t g_TFcard_status; // 0: TF卡未插入, 1: TF卡已插入
extern volatile uint8_t g_maintain_status; // 0: 不保持供电, 1: 保持供电(不断电)
extern volatile uint8_t kv_hdp0_or_spk1; // 0: 耳机, 1: 扬声器
// key.c
extern volatile uint8_t g_key_WKP_RT; // 0: 唤醒按键未按下, 1: 唤醒按键已按下
extern volatile uint8_t g_key_L_M_RT; // 0: 左摇杆未在中间位置, 1: 左摇杆在中间位置
extern volatile uint8_t g_key_R_M_RT; // 0: 右摇杆未在中间位置, 1: 右摇杆在中间位置
// rng.c
extern RTC_DateTypeDef now_date; // 当前日期 (RTC)
extern RTC_TimeTypeDef now_time; // 当前时间 (RTC)
//lcd_pwm.c
extern volatile uint8_t g_pwm_inited; // PWM是否初始化完成标志 0：未初始化，1：已初始化
extern volatile uint8_t kv_screen_status; // 屏幕背光开关 (持久化) 0：关闭，1：开启
extern volatile uint8_t kv_brightness;    // 屏幕亮度 0-255 (持久化)
//max98357.c
extern volatile uint8_t g_max98357_inited; // MAX98357A（喇叭）是否初始化标志 0：未初始化，1：已初始化
extern volatile uint8_t kv_max98357_ststus; // MAX98357A 供电开关 (持久化) 0：关断，1：供电
//adc.c
extern volatile uint8_t g_adc_dma_finished; // ADC DMA传输完成标志
extern volatile uint16_t g_slave_cc1_value; // Type-C Slave CC1电压值 (ADC采样值)
extern volatile uint16_t g_slave_cc2_value; // Type-C Slave CC2电压(ADC采样值)
extern volatile uint16_t g_host_cc1_value;  // Type-C Host CC1电压值 (ADC采样值)
extern volatile uint16_t g_host_cc2_value;  // Type-C Host CC2电压值 (ADC采样值)
extern volatile uint8_t g_usb_status; // Type-C状态 0: 未连接, 1: Slave, 2: Host, 3: AC, 4: CC_IDLE, 5: CC_OKEY, 6: AC_IDLE, 7: AC_OKEY
extern volatile uint8_t g_lvgl_input_disabled; // LVGL输入禁用标志 0: 正常, 1: 禁用
extern volatile float g_battery_voltage; // 电池电压 (单位: V)
extern volatile int16_t g_key_L_X; // 左摇杆 X 轴
extern volatile int16_t g_key_L_Y; // 左摇杆 Y 轴
extern volatile int16_t g_key_R_X; // 右摇杆 X 轴
extern volatile int16_t g_key_R_Y; // 右摇杆 Y 轴
// sdio_sdcard.c
extern volatile uint8_t g_TFcard_inited; // TF卡初始化标志 0=未初始化 1=已初始化
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

extern RTC_DateTypeDef now_date; //RTC_WeekDay  RTC_Month  RTC_Date  RTC_Year
extern RTC_TimeTypeDef now_time; //RTC_Hours  RTC_Minutes  RTC_Seconds  RTC_H12
extern Lunar_t now_lunar; //农历

//lcd_bsp.c
extern volatile uint8_t g_lcd_user; // 当前LCD使用者标识

// es9018k2m.c
extern volatile uint8_t g_es9018_inited;     // ES9018初始化标志
extern volatile uint8_t kv_es9018_status;    // ES9018 供电开关 (持久化) 0：关断，1：供电
extern volatile uint8_t music_bitdepth;      // 音频位深 16/24/32
extern volatile uint8_t kv_hdp_value;        // 耳机音量 (0-255)
extern volatile uint8_t kv_spk_value;         // 扬声器音量 (持久化) 0-255
extern volatile uint8_t kv_es9018_volume;    // ES9018 DAC 音量缓存
extern volatile ES9018_Config_t kv_es9018_cfg; // ES9018 DAC 配置
// fontupd.c
extern volatile uint8_t g_font_update_state;      // 字库更新状态: 0=空闲 1=擦除 2=写入 3=完成 0xFF=错误
extern volatile uint8_t g_font_update_progress;   // 字库更新进度 0-100
extern volatile uint8_t g_font_update_file_index; // 当前更新文件索引
extern volatile uint8_t g_font_update_error;      // 字库更新错误码

//flashdb
extern struct fdb_kvdb kvdb;//flashdb kvdb 操作结构体
extern struct fdb_tsdb tsdb;//flashdb tsdb 操作结构体

//v2p_bat.c
extern volatile int8_t g_battery_percent; // 电池剩余电量百分比

//task manager.c
extern volatile uint8_t Basic_Task_Status; // 基础任务状态
extern volatile uint8_t LVGL_Task_Status; // LVGL任务状态
extern volatile uint8_t USB_Task_Status; // USB任务状态
extern volatile uint8_t Music_Task_Status; // 音乐任务状态
extern volatile uint8_t Game_Task_Status; // 游戏任务状态
extern volatile uint8_t Media_Task_Status; // 媒体任务状态
extern volatile uint8_t Font_Task_Status; // 字体任务状态
extern volatile uint8_t FileOp_Task_Status; // 文件操作任务状态

//fontupd
extern volatile uint8_t g_font_need_update; // 字库是否需要更新

//usb hid
extern volatile int16_t g_usb_joy_L_X; // USB手柄左摇杆X轴
extern volatile int16_t g_usb_joy_L_Y; // USB手柄左摇杆Y轴
extern volatile int16_t g_usb_joy_R_X; // USB手柄右摇杆X轴
extern volatile int16_t g_usb_joy_R_Y; // USB手柄右摇杆Y轴
extern volatile int16_t g_usb_mouse_dx; // USB鼠标X轴移动量
extern volatile int16_t g_usb_mouse_dy; // USB鼠标Y轴移动量
extern volatile uint8_t g_usb_mouse_btn; // USB鼠标按键状态

//debug.c
extern volatile uint8_t kv_debug_mode; // 调试输出模式 (Debug_Mode_None/TSDB/USBD/LVGL)

//file_unit
extern char *current_path; // 文件浏览器当前路径

//status_bar.c
extern volatile uint8_t g_VorP; // 状态栏电池区显示模式 0-显示电压 1-显示百分比

//music.c
extern volatile uint8_t Music_Suspend_Flag; // 音乐暂停标志 0: 播放中, 1: 暂停
extern volatile uint8_t Music_Status;         // 音乐播放状态 (Music_None/Song_*/Music_Exit)

//keyboard.c
extern volatile uint8_t g_host_kbd_key;      // 接收到的物理键盘键码 (USB Host HID)
extern volatile uint8_t g_host_kbd_mod;      // 接收到的物理键盘修饰键 (Shift等)
extern volatile uint8_t g_host_kbd_trigger;  // 物理键盘按键触发标志 0:无 1:有

extern volatile uint8_t g_usb_kbd_modifier; // 向外发送的修饰键 (Shift, Ctrl, Alt 等)
extern volatile uint8_t g_usb_kbd_key;      // 向外发送的键码 (Keycode)
extern volatile uint8_t g_usb_kbd_trigger;  // 向外发送状态机: 0=空闲 1=请求按下 2=请求松开

//usb
extern volatile uint8_t g_usb_function; // USB功能 (USB_NONE/USBD_*/USBH_*)

#endif // __VARIABLES_H__
