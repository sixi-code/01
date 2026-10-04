#include "stm32f4xx.h"
#include "lv_port_disp.h"
#include "page_manager.h"
#include "canvas_list_page.h"
#include "canvas_list_unit.h"
#include "status_bar.h"
#include "navigation_bar.h"

void Create_CANVAS_List_Page(void)
{
	Create_Canvas_List_Unit();
	Create_Navigation_Bar("画板");
	Create_Status_Bar();
}

void Update_CANVAS_List_Page(void)
{
	Update_Canvas_List_Unit();
	Update_Navigation_Bar();
	Update_Status_Bar();
}

void Remove_CANVAS_List_Page(void)
{
	Remove_Canvas_List_Unit();
	Remove_Navigation_Bar();
	Remove_Status_Bar();
}

// 导出页面接口
const Page_Interface_t page_canvas_list_interface = {
    .id = PAGE_CANVAS_LIST,
    .init = Create_CANVAS_List_Page,
    .update = Update_CANVAS_List_Page,
    .exit = Remove_CANVAS_List_Page,
    .can_exit = NULL, // 列表页没有需要确认的内容
};
