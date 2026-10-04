#include "stm32f4xx.h"
#include "lv_port_disp.h"
#include "page_manager.h"
#include "note_page.h"
#include "note_unit.h"
#include "status_bar.h"
#include "navigation_bar.h"

void Create_NOTE_Page(void)
{
	Create_Note_Unit();
	Create_Navigation_Bar("记事本");
	Create_Status_Bar();
}

void Update_NOTE_Page(void)
{
	Update_Note_Unit();
	Update_Navigation_Bar();
	Update_Status_Bar();
}

void Remove_NOTE_Page(void)
{
	Remove_Note_Unit();
	Remove_Navigation_Bar();
	Remove_Status_Bar();
}

// 离开本页面前询问: 记事本有未保存的改动时先弹确认框, 拦下本次页面切换
bool Can_Exit_NOTE_Page(void)
{
	return Note_Unit_Can_Exit();
}

// 导出页面接口
const Page_Interface_t page_note_interface = {
    .id = PAGE_NOTE,
    .init = Create_NOTE_Page,
    .update = Update_NOTE_Page,
    .exit = Remove_NOTE_Page,
    .can_exit = Can_Exit_NOTE_Page,
};
