#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "lunar.h"
#include "event_groups.h"
#include "es9018k2m.h"
#include "defines.h"
#include "flashdb.h"

//freertos相关
//互斥锁/信号量 统一定义,各自初始化
SemaphoreHandle_t xFlashMutex = NULL;//w25q128互斥锁
SemaphoreHandle_t xFlashSemaphore = NULL;//w25q128计数型信号量
SemaphoreHandle_t xI2SSemaphore = NULL;//music dma 传输完成信号量
SemaphoreHandle_t xIICMutex = NULL;//iic互斥锁
SemaphoreHandle_t xSDcardMutex = NULL;//sdcard互斥锁
SemaphoreHandle_t xSDcardSemaphore = NULL;//sdcard计数型信号量
SemaphoreHandle_t xBSCMutex = NULL;//tlsf bsc互斥锁
SemaphoreHandle_t xCCMMutex = NULL;//tlsf ccm互斥锁
SemaphoreHandle_t xFDBSemaphore = NULL;//flashdb互斥锁
SemaphoreHandle_t xTaskManagerSemaphore = NULL;//taskmanager信号量
EventGroupHandle_t xLcdEventGroup = NULL; // lcd事件组

//freertos所有任务句柄
TaskHandle_t Basic_Task_handler   = NULL;
TaskHandle_t Lvgl_Task_handler    = NULL;
TaskHandle_t USB_Task_handler     = NULL;
TaskHandle_t Music_Task_handler   = NULL;
TaskHandle_t Media_Task_handler   = NULL;
TaskHandle_t Game_Task_handler    = NULL;
TaskHandle_t Font_Task_handler    = NULL;
TaskHandle_t FileOp_Task_handler  = NULL;
TaskHandle_t Start_Task_handler   = NULL;
TaskHandle_t Task_Manager_handler = NULL;

//pin_ctrl.c
volatile uint8_t g_charge_status = 0; // 0: 未充电, 1: 充电中, 2: 充电完成
volatile uint8_t g_vbus_status = 0;    // 0: usb充电未连接, 1: 已连接 (usb不向外供电时有效 0-低电平 1-高电平)
volatile uint8_t g_headphone_status = 0; // 0: 耳机未插入, 1: 耳机已插入
volatile uint8_t g_TFcard_status = 0; // 0: TF卡未插入, 1: TF卡已插入
volatile uint8_t kv_hdp0_or_spk1 = 0; // 0: 耳机, 1: 扬声器
//key.c
volatile uint8_t g_key_L_M_RT = 0; // 0: 左摇杆未在中间位置, 1: 左摇杆在中间位置
volatile uint8_t g_key_R_M_RT = 0; // 0: 右摇杆未在中间位置, 1: 右摇杆在中间位置
volatile uint8_t g_key_WKP_RT = 0; // 0: 唤醒按键未按下, 1: 唤醒按键已按下
//rng.c
RTC_DateTypeDef now_date; // 当前日期 (RTC)
RTC_TimeTypeDef now_time; // 当前时间 (RTC)
//lcd_pwm.c
volatile uint8_t g_pwm_inited = 0; // LCD PWM是否初始化完成标志 0：未初始化，1：已初始化
volatile uint8_t kv_screen_status = 0; // 屏幕背光开关 (持久化) 0：关闭，1：开启
volatile uint8_t kv_brightness = 32;   // 屏幕亮度 0-255 (持久化)
//max98357.c
volatile uint8_t g_max98357_inited = 0; // MAX98357A（喇叭）是否初始化 0：未初始化，1：已初始化
volatile uint8_t kv_max98357_ststus = 0; // MAX98357A 供电开关 (持久化) 0：关断，1：供电
//adc.c
volatile uint8_t g_adc_dma_finished = 0; // ADC DMA传输完成标志 0：未完成，1：已完成
volatile int16_t g_key_L_X = 0; // 左摇杆 X 轴
volatile int16_t g_key_L_Y = 0; // 左摇杆 Y 轴
volatile int16_t g_key_R_X = 0; // 右摇杆 X 轴
volatile int16_t g_key_R_Y = 0; // 右摇杆 Y 轴
// sdio_sdcard.c
volatile uint8_t g_TFcard_inited = 0; // TF卡初始化标志 0=未初始化 1=已初始化
volatile uint16_t g_slave_cc1_value = 0; // Type-C Slave CC1电压值 (ADC采样值)
volatile uint16_t g_slave_cc2_value = 0; // Type-C Slave CC2电压值 (ADC采样值)
volatile uint16_t g_host_cc1_value = 0;  // Type-C Host CC1电压值 (ADC采样值)
volatile uint16_t g_host_cc2_value = 0;  // Type-C Host CC2电压值 (ADC采样值)
volatile uint8_t g_usb_status = 0; // Type-C连接状态: 0=未连接 1=CtoC空闲 2=AtoC空闲 3=AtoC设备模式 4=CtoC设备模式 5=直接主机模式 6=CtoC主机模式
volatile uint8_t g_lvgl_input_disabled = 0; // LVGL输入禁用标志 0: 正常, 1: 禁用
volatile float g_battery_voltage = 0.0f; // 电池电压 (单位: V)

//systick_conf.c
volatile uint32_t RTOS_OK = 0; // FreeRTOS调度器状态 0：未启动，1：已启动
//rtc_clock.h
volatile uint8_t RTC_HFmt = 0;  //0-24 1-12 时间格式 1：12小时制 0：24小时制
volatile uint8_t RTC_Week = 7;  //1-7 星期
volatile uint8_t RTC_Year = 27; //0-99 年
volatile uint8_t RTC_Moth = 8;  //1-12 月
volatile uint8_t RTC_Date = 30; //1-31 日
volatile uint8_t RTC_Hour = 16; //0-24 小时
volatile uint8_t RTC_Mint = 00; //0-60 分钟
volatile uint8_t RTC_Secd = 0;  //0-60 秒

Lunar_t now_lunar; //农历

//lcd_bsp.c
volatile uint8_t g_lcd_user = LCD_USER_LVGL;// 当前LCD使用者标识

// es9018k2m.c
volatile uint8_t g_es9018_inited = 0;     // ES9018初始化标志
volatile uint8_t kv_es9018_status = 0;    // ES9018 供电开关 (持久化) 0：关断，1：供电
volatile uint8_t music_bitdepth = 24;      // 音频位深 16/24/32
volatile uint8_t kv_hdp_value = 128;       // 耳机音量 (0-255)
volatile uint8_t kv_es9018_volume = 128;   // ES9018 DAC 音量缓存
volatile ES9018_Config_t kv_es9018_cfg = {0,0,104,2,0,0,0,0,0,5,0,1,5,1,0,0,0,0}; // ES9018 DAC 配置
// fontupd.c
volatile uint8_t g_font_update_state = 0;      // 字库更新状态: 0=空闲 1=擦除 2=写入 3=完成 0xFF=错误
volatile uint8_t g_font_update_progress = 0;   // 字库更新进度 0-100
volatile uint8_t g_font_update_file_index = 0; // 当前更新文件索引
volatile uint8_t g_font_update_error = 0;      // 字库更新错误码

//flashdb
struct fdb_kvdb kvdb = { 0 };//flashdb kvdb 操作结构体
struct fdb_tsdb tsdb = { 0 };//flashdb tsdb 操作结构体

//v2p_bat.c
volatile int8_t g_battery_percent = 0; // 电池剩余电量百分比

//task manager.c
volatile uint8_t Basic_Task_Status = Task_P_Null; // 基础任务状态
volatile uint8_t LVGL_Task_Status = Task_P_Null; // LVGL任务状态
volatile uint8_t USB_Task_Status = Task_P_Null; // USB任务状态
volatile uint8_t Music_Task_Status = Task_P_Null; // 音乐任务状态
volatile uint8_t Game_Task_Status = Task_P_Null; // 游戏任务状态
volatile uint8_t Media_Task_Status = Task_P_Null; // 媒体任务状态
volatile uint8_t Font_Task_Status = Task_P_Null; // 字体任务状态
volatile uint8_t FileOp_Task_Status = Task_P_Null; // 文件操作任务状态

//fontupd
volatile uint8_t g_font_need_update = 0; // 字库是否需要更新

//usb hid
volatile int16_t g_usb_joy_L_X = 0; // USB手柄左摇杆X轴
volatile int16_t g_usb_joy_L_Y = 0; // USB手柄左摇杆Y轴
volatile int16_t g_usb_joy_R_X = 0; // USB手柄右摇杆X轴
volatile int16_t g_usb_joy_R_Y = 0; // USB手柄右摇杆Y轴
volatile int16_t g_usb_mouse_dx = 0; // USB鼠标X轴移动量
volatile int16_t g_usb_mouse_dy = 0; // USB鼠标Y轴移动量
volatile uint8_t g_usb_mouse_btn = 0; // USB鼠标按键状态

//debug.c
volatile uint8_t kv_debug_mode = Debug_Mode_None; // 调试输出模式
