#include "lv_port_indev.h"
#include "lvgl.h"
#include "adc.h"
#include "key.h"
#include "variables.h"
#include <math.h>
#include <stdbool.h>
#include "variables.h"
#include <stdlib.h>

#define HOR_RES      240  /* 屏幕宽度 */
#define VER_RES      240  /* 屏幕高度 */

static void mouse_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data);

lv_indev_t * indev_mouse;
static lv_obj_t * mouse_cursor; 

float cursor_x = HOR_RES / 2.0f;
float cursor_y = VER_RES / 2.0f; 

static lv_indev_drv_t indev_drv;

void lv_port_indev_init(void)
{
	lv_indev_drv_init(&indev_drv);
	indev_drv.type = LV_INDEV_TYPE_POINTER;
	indev_drv.read_cb = mouse_read;

	indev_mouse = lv_indev_drv_register(&indev_drv);

    // 创建鼠标视觉小黑点
	mouse_cursor = lv_obj_create(lv_layer_sys());// 将鼠标小黑点添加到系统层
	lv_obj_set_size(mouse_cursor, 9, 9);// 设置鼠标小黑点的大小
	lv_obj_set_style_radius(mouse_cursor, LV_RADIUS_CIRCLE, 0);// 设置鼠标小黑点为圆形
	lv_obj_set_style_bg_opa(mouse_cursor, LV_OPA_0, 0); // 设置鼠标小黑点的背景透明
	lv_obj_set_style_border_width(mouse_cursor, 1, 0);  // 设置鼠标小黑点的边框宽度
	lv_obj_set_style_border_color(mouse_cursor, lv_color_black(), 0); // 设置鼠标小黑点的边框颜色为黑色
	
	lv_obj_clear_flag(mouse_cursor, LV_OBJ_FLAG_CLICKABLE);// 设置鼠标小黑点不可点击
}

// 读取鼠标输入数据的回调函数
static void mouse_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    if (g_lvgl_input_disabled)// 如果LVGL输入被禁用，则不更新鼠标位置和状态 
	{
		static uint8_t last_R = 0;// 记录上一次右摇杆中间位置状态
        if (last_R && !g_key_R_M_RT) // 如果上一次右摇杆在中间位置，而这次不在中间位置，则表示右摇杆刚刚离开中间位置
		{
            g_lvgl_input_disabled = 0;// 启用LVGL输入
			last_R = 0;// 更新上一次右摇杆中间位置状态为不在中间位置
        } 
		else 
		{
			last_R = g_key_R_M_RT;// 更新上一次右摇杆中间位置状态为当前状态
            data->state = LV_INDEV_STATE_REL;// 设置鼠标状态为释放状态
            return;
        }
    }
	
    int16_t usb_dx = g_usb_mouse_dx; g_usb_mouse_dx = 0;// 获取USB鼠标X轴移动量，并清零
    int16_t usb_dy = g_usb_mouse_dy; g_usb_mouse_dy = 0;// 获取USB鼠标Y轴移动量，并清零
    uint8_t usb_btn = g_usb_mouse_btn;// 获取USB鼠标按键状态

    /* --------- 1. 右摇杆：仅负责鼠标光标移动 --------- */
    float move_x = (g_key_R_X * 0.06f) + (g_usb_joy_R_X * 0.06f) + usb_dx;
    float move_y = -(g_key_R_Y * 0.06f) - (g_usb_joy_R_Y * 0.06f) + usb_dy; 

    cursor_x += move_x;
    cursor_y += move_y;

    // 限制鼠标光标在屏幕范围内
    if(cursor_x < 0) cursor_x = 0;
    if(cursor_y < 0) cursor_y = 0;
    if(cursor_x > HOR_RES - 1) cursor_x = HOR_RES - 1;
    if(cursor_y > VER_RES - 1) cursor_y = VER_RES - 1;

    // 将鼠标光标位置传递给LVGL
    data->point.x = (lv_coord_t)cursor_x;
    data->point.y = (lv_coord_t)cursor_y;

    /* --------- 2. 左摇杆：仅负责触发点击(按下)状态 --------- */
    int16_t combined_L_X = g_key_L_X + g_usb_joy_L_X;
    int16_t combined_L_Y = g_key_L_Y + g_usb_joy_L_Y;
    
    // 只要左摇杆向任意方向拨动超过死区（例如50），即视为“鼠标左键按下”
    bool is_clicked = false;
    if (abs(combined_L_X) > 50 || abs(combined_L_Y) > 50 || (usb_btn & 0x01)) {
        is_clicked = true;
    }

    data->state = is_clicked ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;// 设置鼠标状态为按下或释放

    /* --------- 3. 其他物理按键的辅助功能 --------- */
	static uint8_t last_L = 0;
	if(!g_key_L_M_RT && last_L)
	{
		/* 
            返回上一页
		    Page_Back();函数待实现
            TODO
        */
        }
	last_L = g_key_L_M_RT;

    /* --------- 4. 实时更新屏幕上的鼠标小黑点 --------- */
    lv_obj_set_pos(mouse_cursor, (lv_coord_t)cursor_x - 4, (lv_coord_t)cursor_y - 4);
}
