#include "main.h"

uint8_t init_res = 0;
int main(void)
{	//！！！
	//SCB->VTOR = 0x08010000;// 重定向中断向量表到 APP 起始地址
	//__enable_irq();// 重新开启全局中断 (因为 Bootloader 跳转前把它关了)
	//不要手贱删除

	Nvic_Init();    //NVIC
	Systick_init(); //systick
	init_res = tlsf_init(); //mem
	
	if(init_res) while(1);
	xTaskCreate((TaskFunction_t		    )   Start_Task,
				(char *                 )   "Start_Task",
				(configSTACK_DEPTH_TYPE )   START_TASK_STACK_SIZE,
				(void *                 )   NULL,
				(UBaseType_t            )   START_TASK_PRIO,
				(TaskHandle_t *         )   &Start_Task_handler );
	vTaskStartScheduler();
				
	while(1){
	}
}	
		
void Start_Task( void * pvParameters )
{
	//此处只初始化上电后不再deinit的硬件
	Wakeup_Key_Init();
	Joystick_Middle_Init();
	Pin_Ctrl_Init();
	RNG_Init();
	ADC1_DMA_Init();

	init_res = RTC_Clock_Init();
	
	//w25q128
	init_res = W25QXX_Init();

	// FlashDB KVDB Init
    fdb_kvdb_control(&kvdb, FDB_KVDB_CTRL_SET_LOCK, (void *)fdb_lock);//设置锁函数
    fdb_kvdb_control(&kvdb, FDB_KVDB_CTRL_SET_UNLOCK, (void *)fdb_unlock);//设置解锁函数
    init_res = fdb_kvdb_init(&kvdb, "env", "fdb_kvdb1", NULL, NULL);//初始化KVDB,不设置默认KV
    kvdb_persist_load();//加载持久化数据
    I2S_Exchange_Ctrl(kv_hdp0_or_spk1);//设置I2S输出模式(从FlashDB读取)
    ES9018_Set_Config((const ES9018_Config_t *)&kv_es9018_cfg);//设置ES9018配置(从FlashDB读取)

    // FlashDB TSDB Init
    fdb_tsdb_control(&tsdb, FDB_TSDB_CTRL_SET_LOCK, (void *)fdb_lock);//设置锁函数
    fdb_tsdb_control(&tsdb, FDB_TSDB_CTRL_SET_UNLOCK, (void *)fdb_unlock);//设置解锁函数
    init_res = fdb_tsdb_init(&tsdb, "log", "fdb_tsdb1", get_fdb_time, 128, NULL); //初始化TSDB,设置时间获取函数为get_fdb_time,最大日志长度为128字节
	
	//font
	g_font_need_update = font_init();//初始化字体,返回是否需要更新字体

	if(init_res) while(1){} //如果·初始化失败,则进入死循环（超时触发看门狗）
		
	//creat task manager
    xTaskCreate(Task_Manager,"Task_Manager",TASK_MANAGER_STACK_SIZE,NULL,TASK_MANAGER_PRIO,&Task_Manager_handler );
	xTaskManagerSemaphore = xSemaphoreCreateBinary();

	Taskmanager_Ctrl(Task_N_Basic, Task_T_Creat, 0);    //basic task
	Taskmanager_Ctrl(Task_N_LVGL, Task_T_Creat, 0);     //LVGL task
	Taskmanager_Ctrl(Task_N_Music, Task_T_Creat, 0);     //MUSIC task
	Taskmanager_Ctrl(Task_N_USB, Task_T_Creat, 0);      //usb task
	Taskmanager_Ctrl(Task_N_Font, Task_T_Creat, 0);     //font task
	Taskmanager_Ctrl(Task_N_FileOp, Task_T_Creat, 0);   //file operation worker

	vTaskDelete(NULL);   //参数为NULL时，表示删除任务自身
}
