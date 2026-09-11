#include "FreeRTOS.h"
#include "task.h"
#include "pin_ctrl.h"
#include "key.h"
#include "adc.h"
#include "es9018k2m.h"
#include "max98357A.h"
#include "rtc_clock.h"
#include "v2p_bat.h"
#include "main.h"
#include "lcd_pwm.h"
#include "sdio_sdcard.h"
#include "tsdb_log.h"
#include "kvdb_ctrl.h"

//此task完成以下轮询读取和处理

//充电检测
//耳机插入检测
//TF卡插入检测

//USB插入检测
//按键读取处理
//摇杆读取处理

//es9018k2m gpio状态
//es9018k2m 更新到芯片
//音乐频谱计算

static uint8_t last_music_bitdepth = 0;// 记录上一次的音乐位深度
static uint8_t last_hdp_value = 0;// 记录上一次的耳机音量值
static uint8_t last_brightness = 0;// 记录上一次的屏幕亮度值

void Basic_Task( void * pvParameters )
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    static uint8_t count100ms = 0;// 100ms计数器
	
    //IWTG_Init();
	
    while(1) 
    {
		//IWDG_Feed();
		
		//adc读取
		//电量计算
        if(g_adc_dma_finished) 
        {
            g_adc_dma_finished = 0;
            ADC_Calculate_Voltage();// 电池电压计算
            VoltageToPercent();// 电池电量百分比计算
        }
		ADC_StartConversion();// 开始下一次ADC转换
		
		//引脚读取
        Is_Battery_Charging();// 充电状态检测
        Is_Headphone_Connecting();// 耳机插入状态检测
        Is_TFcard_Connecting(); // TF卡插入状态检测
        Read_Wakeup_Key();// 唤醒按键读取
        Get_Joystick_Middle();// 摇杆中间位置读取
		
		//tf卡插入卸载
		if(!g_TFcard_inited && g_TFcard_status)
		{
			if(!SD_Init()) fatfs_mount(DEV_SD);// TF卡初始化成功后挂载文件系统
			else SD_Deinit();// TF卡初始化失败则立即卸载
		}
		if(g_TFcard_inited && !g_TFcard_status)// TF卡拔出则卸载文件系统
		{
			if(!SD_Deinit()) fatfs_unmount(DEV_SD);// TF卡卸载成功后卸载文件系统
		}

		//pwm ctrl
		if(!g_pwm_inited && kv_screen_status)// 如果PWM未初始化且屏幕状态为开启，则初始化PWM
		{
			LCD_TIM8_PWM_Init();// 初始化PWM
			LCD_PWM_SetFrequency(10000);// 设置PWM频率为10kHz
		}
		if(g_pwm_inited && !kv_screen_status)// 如果PWM已初始化且屏幕状态为关闭，则卸载PWM
		{
			LCD_PWM_DeInit();// 卸载PWM
		}
		
		//max98357 ctrl
		if(!g_max98357_inited && kv_max98357_ststus)// 如果MAX98357A未初始化且状态为开启，则初始化MAX98357A
		{
			MAX98357_Init();
		}
		if(g_max98357_inited && !kv_max98357_ststus)// 如果MAX98357A已初始化且状态为关闭，则卸载MAX98357A
		{
			MAX98357_Deinit();
		}
		
		//es9018 ctrl
		if(!g_es9018_inited && kv_es9018_status)// 如果ES9018未初始化且状态为开启，则初始化ES9018
		{
			if(ES9018_Init()) ES9018_Deinit();
		}
		if(g_es9018_inited && !kv_es9018_status)// 如果ES9018已初始化且状态为关闭，则卸载ES9018
		{
			ES9018_Deinit();
		}
		
		//es9018
		if(last_music_bitdepth != music_bitdepth) // 如果音乐位深度发生变化，则更新ES9018的位深度
		{
			if(!ES9018_Set_BitDepth(music_bitdepth))
				last_music_bitdepth = music_bitdepth;
		}
		if(last_hdp_value != kv_hdp_value) // 如果耳机音量值发生变化，则更新ES9018的音量
		{
			if(!ES9018_Set_Volume(kv_hdp_value,kv_hdp_value))
				last_hdp_value = kv_hdp_value;
		}
		ES9018_Update_Register();// 更新ES9018的寄存器配置，如果配置参数发生变化，则写入新的配置
		if(last_brightness != kv_brightness) // 如果屏幕亮度值发生变化，则更新PWM的占空比
		{
			LCD_PWM_SetDutyCycle(kv_brightness);
			last_brightness = kv_brightness;
		}
		
		//rtc
		static uint8_t last_M = 0;// 记录上一次的唤醒按键状态
		// 如果唤醒按键从按下状态变为未按下状态，则更新RTC时钟
		if(!g_key_WKP_RT && last_M)
		{
			//进入休眠模式 TODO
		}
		last_M = g_key_WKP_RT;
		// 每500ms更新一次时钟
        if(++count100ms > 4)
        {
            count100ms = 0;
            RTC_Clock_Update();
        }

        tsdb_log_flush();// 将日志缓冲区中的数据写入存储设备
        kvdb_persist_flush();// 将所有已修改的持久化条目写入kvdb

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(20));
    }
}
