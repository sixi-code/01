#ifndef __DEFINES_H
#define __DEFINES_H

//定义 USB Type-C状态 (host:本机CC1+CC2值  device:对端CC1+CC2值)
#define TYPEC_NO_FIND 0 //host:4095+4095  		device:2+2   		// 未连接
#define TYPEC_CC_IDLE 1 //host:4000+120 		device:2+2   		// C to C线缆已连接(空闲)
#define TYPEC_AC_IDLE 2 //host:4076+2490 		device:2+2   		// A to C线缆已连接(空闲)
#define TYPEC_AC_OKEY 3 //host:4076+4095  		device:2+500        // A to C线缆,作为设备连接
#define TYPEC_CC_OKEY 4 //host:120+500/3972 	device:2+1150/2050  // C to C线缆,作为设备连接
#define TYPEC_IS_HOST 5 //host:4076+500     	device:2+2          // 直接作为主机连接
#define TYPEC_CC_HOST 6 //host:500+120      	device:2+2          // 通过CtoC线缆作为主机连接

//定义 LCD 使用者标识
#define LCD_USER_LVGL (1 << 0)// LVGL图形库
#define LCD_USER_DISP (1 << 1)// 显示器
#define LCD_USER_MDIA (1 << 2)// 媒体播放器
#define LCD_USER_GAME (1 << 3)// 游戏

//基础任务
#define BASIC_PRIO         3
#define BASIC_STACK_SIZE   512

//LVGL任务
#define LVGL_PRIO         2
#define LVGL_STACK_SIZE   512

//媒体任务
#define MEDIA_PRIO         2
#define MEDIA_STACK_SIZE   512

//游戏任务
#define GAME_PRIO         2
#define GAME_STACK_SIZE   512

//USB任务
#define USB_PRIO          4  
#define USB_STACK_SIZE    512

//USB端点0线程 优先级5
//USB主机PSC线程 优先级5

//音乐任务
#define MUSIC_PRIO         6
#define MUSIC_STACK_SIZE   1024

//任务管理器
#define TASK_MANAGER_PRIO         7
#define TASK_MANAGER_STACK_SIZE   512

//启动任务
#define START_TASK_PRIO         8
#define START_TASK_STACK_SIZE   512

//字体任务
#define FONT_PRIO         1
#define FONT_STACK_SIZE   512

//文件操作任务 (后台文件复制/删除工作线程)
#define FILEOP_PRIO         1
#define FILEOP_STACK_SIZE   512

//定义 RTOS 任务设置
#define Task_N_Basic     0
#define Task_N_LVGL      1
#define Task_N_USB       2
#define Task_N_Music     3
#define Task_N_Media     4
#define Task_N_Game      5
#define Task_N_Font      6
#define Task_N_FileOp    7


//...定义任务管理器:任务状态...// (T-临时 P-持久)
#define Task_P_Null      0 //0-空 (初始值,5后自动设置)
#define Task_T_Creat     1 //1-创建任务
#define Task_P_Running   2 //2-任务运行中 (1/5后自动设置)
#define Task_T_Suspend   3 //3-挂起任务
#define Task_P_Stop 	 4 //4-任务已挂起 (3后自动设置)
#define Task_T_Resume 	 5 //5-恢复任务 (当任务被挂起时)
#define Task_T_Delete    6 //6-删除任务

#endif // __DEFINES_H
