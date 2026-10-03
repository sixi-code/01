#include "stm32f4xx.h"
#include "page_manager.h"
#include "log_ctrl_unit.h"
#include "status_bar.h"
#include "navigation_bar.h"

static void Main_Page_Init(void)
{
	Create_Log_Control();
	Create_Navigation_Bar("日志设置");
	Create_Status_Bar();
}

static void Main_Page_Update(void)
{
	Update_Log_Control();
	Update_Navigation_Bar();
	Update_Status_Bar();
}

static void Main_Page_Exit(void)
{
	Remove_Log_Control();
	Remove_Navigation_Bar();
	Remove_Status_Bar();
}

// 页面接口定义
const Page_Interface_t page_log_ctrl_interface = {
    .id = PAGE_LOG_CTRL,
    .init = Main_Page_Init,
    .update = Main_Page_Update,
    .exit = Main_Page_Exit,
};
