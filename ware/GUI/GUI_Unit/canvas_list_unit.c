#include "lvgl.h"
#include "page_manager.h"
#include "list_unit.h"
#include "defines.h"
#include "canvas_list_unit.h"

// 点列表里的某张画作：把画作名交给画布页（与记事本传笔记名同一机制）
static void canvas_list_item_cb(const char * name)
{
	Page_Request_Switch(PAGE_CANVAS, name);
}

// 点「新建」：空画作名表示起一张空白画布
static void canvas_list_new_cb(void)
{
	Page_Request_Switch(PAGE_CANVAS, "");
}

// 画作列表交给通用列表组件时用的配置
static const List_Cfg_t canvas_list_cfg = {
	.dir          = CANVAS_DIR_PATH,
	.ext          = CANVAS_EXT,
	.page_size    = 5,
	.name_max     = CANVAS_NAME_MAX,
	.file_max     = CANVAS_FILE_MAX,
	.framed       = 0,// 占的是页面内容区, 不要边框
	.empty_text   = "还没有画作\n点右上角「新建」画一张",
	.act_text     = "新建",
	.tip_too_long = "文件名过长\n无法打开这幅画作",
	.tip_too_big  = "文件过大\n不是本机画作格式",
	.on_item      = canvas_list_item_cb,
	.on_act       = canvas_list_new_cb,
};

void Create_Canvas_List_Unit(void)
{
	List_Create(lv_scr_act(), &canvas_list_cfg);
}

void Update_Canvas_List_Unit(void)
{
	// 列表没有后台动作要收尾, 保留空实现与页面接口对齐
}

void Remove_Canvas_List_Unit(void)
{
	List_Destroy();
}
