#include "FreeRTOS.h"
#include "task.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

void Lvgl_Task( void * pvParameters )
{   // 初始化 LVGL 库和显示输入设备
	lv_init();
	lv_port_disp_init();
	lv_port_indev_init();
    
    //Page_Manager_Init(); 初始化页面管理器 TODO


    TickType_t xLastWakeTime = xTaskGetTickCount();
	
    while(1) 
    {
        //Page_Manager_Loop(); 页面管理器主循环 TODO
        
        lv_timer_handler(); 
		
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(20));
    }
}
