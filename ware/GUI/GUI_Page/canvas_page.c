#include "stm32f4xx.h" 
#include "lv_port_disp.h"
#include "page_manager.h"
#include "status_bar.h"
#include "navigation_bar.h"
#include "canvas_unit.h"

void Create_Canvas_Page(void)
{
	Create_Canvas_Unit();
}

void Update_Canvas_Page(void)
{
	Update_Canvas_Unit();
}

void Remove_Canvas_Page(void)
{
	Remove_Canvas_Unit();
}

// 离开本页面前询问: 画布有未保存的改动时先弹确认框, 拦下本次页面切换
bool Can_Exit_Canvas_Page(void)
{
	return Canvas_Unit_Can_Exit();
}

// 导出页面接口
const Page_Interface_t page_canvas_interface = {
    .id = PAGE_CANVAS,
    .init = Create_Canvas_Page,
    .update = Update_Canvas_Page,
    .exit = Remove_Canvas_Page,
    .can_exit = Can_Exit_Canvas_Page,
};
