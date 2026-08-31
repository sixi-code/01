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

#endif // __DEFINES_H