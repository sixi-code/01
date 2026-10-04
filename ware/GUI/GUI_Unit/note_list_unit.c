#include "lvgl.h"
#include "page_manager.h"
#include "list_unit.h"
#include "defines.h"
#include "note_list_unit.h"

// 点列表里的某篇笔记：把笔记名交给编辑页（与文件管理器传目录路径同一机制）
static void note_list_item_cb(const char * name)
{
	Page_Request_Switch(PAGE_NOTE, name);
}

// 点「新建」：空笔记名表示起一篇新的
static void note_list_new_cb(void)
{
	Page_Request_Switch(PAGE_NOTE, "");
}

// 记事本列表交给通用列表组件时用的配置
static const List_Cfg_t note_list_cfg = {
	.dir          = NOTE_DIR_PATH,
	.ext          = NOTE_EXT,
	.page_size    = 5,
	.name_max     = NOTE_NAME_MAX,
	.file_max     = NOTE_FILE_MAX,
	.framed       = 0,// 占的是页面内容区, 不要边框
	.empty_text   = "还没有笔记\n点右上角「新建」写第一篇",
	.act_text     = "新建",
	.tip_too_long = "文件名过长\n无法编辑该笔记",
	.tip_too_big  = "文件过大\n只能编辑 4 KB 以内的笔记",
	.on_item      = note_list_item_cb,
	.on_act       = note_list_new_cb,
};

void Create_Note_List_Unit(void)
{
	List_Create(lv_scr_act(), &note_list_cfg);
}

void Update_Note_List_Unit(void)
{
	// 列表没有后台动作要收尾, 保留空实现与页面接口对齐
}

void Remove_Note_List_Unit(void)
{
	List_Destroy();
}
