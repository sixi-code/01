#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include "ff.h"
#include "malloc.h"
#include "list_unit.h"

// 列表几何：页面内容区可用 240x180（导航栏下方 30~210）
#define LIST_W        240
#define LIST_H        180
#define LIST_Y        30
#define LIST_BAR_H    32      // 顶栏高度
#define LIST_ITEM_H   26      // 条目高度
#define LIST_ITEM_GAP 2       // 条目间距
#define LIST_ITEM_W   232     // 条目宽度
#define LIST_LABEL_W  220     // 条目文字宽度（超出直接裁掉）

// 列表状态（同一时刻只会有一个列表，故用单实例）
typedef struct {
	const List_Cfg_t * cfg;      // 当前配置
	lv_obj_t * panel;            // 列表主容器
	lv_obj_t * box;              // 条目滚动容器
	lv_obj_t * btn_prev;         // 上一页
	lv_obj_t * btn_next;         // 下一页
	lv_obj_t * label_page;       // 页码 "2/5"
	uint16_t   page;             // 当前页（0 基）
	uint16_t   page_max;         // 最后一页（0 基）
	uint16_t   path_size;        // 拼完整路径的缓冲区大小
} list_state_t;

static list_state_t ls = {0};

// 提示弹窗（与列表相互独立，可以同时存在）
static lv_obj_t * tip_dlg = NULL;

// 内部函数声明
static void list_item_event_cb(lv_event_t * e);
static void list_item_delete_cb(lv_event_t * e);
static void list_prev_page_cb(lv_event_t * e);
static void list_next_page_cb(lv_event_t * e);
static void list_act_cb(lv_event_t * e);
static void list_dismiss_cb(lv_event_t * e);
static void list_build_path(char * out, const char * name);
static uint8_t list_match(const FILINFO * fno);
static uint32_t list_count(void);
static void list_fill(void);
static void list_update_pager(void);

// 建一个统一样式的小按钮
lv_obj_t * List_Create_Btn(lv_obj_t * parent, const char * txt, int32_t w, int32_t h)
{
	lv_obj_t * btn = lv_btn_create(parent);
	lv_obj_set_size(btn, w, h);
	lv_obj_set_style_bg_color(btn, lv_color_white(), LV_PART_MAIN);
	lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
	lv_obj_set_style_border_color(btn, lv_color_black(), LV_PART_MAIN);
	lv_obj_set_style_radius(btn, 4, LV_PART_MAIN);
	lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
	lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
	lv_obj_set_style_bg_color(btn, lv_color_hex(0xE0E0E0), LV_PART_MAIN | LV_STATE_PRESSED);

	lv_obj_t * label = lv_label_create(btn);
	lv_label_set_text(label, txt);
	lv_obj_set_style_text_font(label, &lv_font_16, 0);
	lv_obj_set_style_text_color(label, lv_color_black(), 0);
	lv_obj_center(label);

	return btn;
}

// 在顶层图层上建一个统一样式的模态卡片
lv_obj_t * List_Create_Dlg(int32_t y_ofs, int32_t height)
{
	lv_obj_t * dlg = lv_obj_create(lv_layer_top());
	lv_obj_set_size(dlg, LIST_W, height);
	lv_obj_align(dlg, LV_ALIGN_TOP_MID, 0, y_ofs);
	lv_obj_set_style_radius(dlg, 6, LV_PART_MAIN);
	lv_obj_set_style_border_width(dlg, 2, LV_PART_MAIN);
	lv_obj_set_style_border_color(dlg, lv_color_black(), LV_PART_MAIN);
	lv_obj_set_style_bg_color(dlg, lv_color_white(), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(dlg, LV_OPA_COVER, LV_PART_MAIN);
	lv_obj_set_style_pad_all(dlg, 0, LV_PART_MAIN);
	lv_obj_set_style_shadow_width(dlg, 0, LV_PART_MAIN);
	lv_obj_clear_flag(dlg, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(dlg, LV_OBJ_FLAG_CLICKABLE);// 挡住下层点击
	return dlg;
}

// 关掉提示弹窗
void List_Close_Tip_Dlg(void)
{
	if (tip_dlg == NULL) return;
	lv_obj_del(tip_dlg);
	tip_dlg = NULL;
}

// 通用提示弹窗：只说明原因
void List_Open_Tip_Dlg(const char * msg)
{
	List_Close_Tip_Dlg();

	tip_dlg = List_Create_Dlg(LIST_Y, 86);

	lv_obj_t * label = lv_label_create(tip_dlg);
	lv_label_set_text(label, msg);
	lv_obj_set_style_text_font(label, &lv_font_16, 0);
	lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(label, 216);
	lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 10);

	lv_obj_t * btn_ok = List_Create_Btn(tip_dlg, "知道了", 72, 26);
	lv_obj_align(btn_ok, LV_ALIGN_TOP_MID, 0, 52);
	lv_obj_add_event_cb(btn_ok, list_dismiss_cb, LV_EVENT_CLICKED, NULL);

	lv_obj_move_to_index(tip_dlg, -1);// 提到最前, 别被键盘点击层挡住
}

uint8_t List_Tip_Is_Open(void)
{
	return (tip_dlg != NULL) ? 1 : 0;
}

uint8_t List_Is_Open(void)
{
	return (ls.panel != NULL) ? 1 : 0;
}

// 拼出某个条目名对应的完整路径
static void list_build_path(char * out, const char * name)
{
	sprintf(out, "%s/%s%s", ls.cfg->dir, name, ls.cfg->ext);
}

// 判断某个目录项是不是本列表要列的文件
static uint8_t list_match(const FILINFO * fno)
{
	const size_t ext_len = strlen(ls.cfg->ext);
	size_t len;

	if (fno->fattrib & (AM_HID | AM_SYS | AM_DIR)) return 0;

	len = strlen(fno->fname);
	if (len <= ext_len) return 0;
	return (strcmp(fno->fname + len - ext_len, ls.cfg->ext) == 0);
}

// 数目录里的条目数
static uint32_t list_count(void)
{
	DIR dir;
	FILINFO fno;
	uint32_t n = 0;

	if (f_opendir(&dir, ls.cfg->dir) != FR_OK) return 0;

	while (1) {
		if (f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == 0) break;
		if (list_match(&fno)) n++;
	}
	f_closedir(&dir);
	return n;
}

// 把当前页的条目填进列表容器
static void list_fill(void)
{
	const size_t ext_len = strlen(ls.cfg->ext);
	DIR dir;
	FILINFO fno;
	int32_t y_ofs = 0;
	uint32_t skip = (uint32_t)ls.page * ls.cfg->page_size;// 本页之前要跳过多少条
	uint32_t seen = 0;// 已扫过的条目数
	uint32_t shown = 0;// 本页已建出的条目数
	const char * tip_text = ls.cfg->empty_text;

	FRESULT res = f_opendir(&dir, ls.cfg->dir);
	if (res != FR_OK) {
		// 目录还没建出来 = 还没存过文件; 其它错误就是卷用不了
		if (res != FR_NO_PATH) tip_text = "SD卡不可用";
	} else {
		while (shown < ls.cfg->page_size) {
			if (f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == 0) break;
			if (!list_match(&fno)) continue;
			if (seen++ < skip) continue;// 还在本页之前

			size_t len = strlen(fno.fname);
			uint8_t too_big = (fno.fsize > ls.cfg->file_max) ? 1 : 0;// 超出上限, 打不开
			uint8_t too_long = ((len - ext_len) > ls.cfg->name_max) ? 1 : 0;// 名字超出能保存的长度

			lv_obj_t * item = lv_obj_create(ls.box);
			lv_obj_set_size(item, LIST_ITEM_W, LIST_ITEM_H);
			lv_obj_set_pos(item, 0, y_ofs);
			lv_obj_set_style_bg_color(item, lv_color_white(), LV_PART_MAIN);
			lv_obj_set_style_border_width(item, 1, LV_PART_MAIN);
			lv_obj_set_style_border_color(item, lv_color_hex(0xBDBDBD), LV_PART_MAIN);
			lv_obj_set_style_radius(item, 3, LV_PART_MAIN);
			lv_obj_set_style_pad_all(item, 0, LV_PART_MAIN);
			lv_obj_set_style_shadow_width(item, 0, LV_PART_MAIN);
			lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
			lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);

			// 条目名随条目一起申请/释放, 供点击回调使用
			char * name = lv_mem_alloc(len - ext_len + 1);
			if (name != NULL) {
				memcpy(name, fno.fname, len - ext_len);
				name[len - ext_len] = '\0';
				lv_obj_add_event_cb(item, list_item_event_cb, LV_EVENT_CLICKED, name);
				lv_obj_add_event_cb(item, list_item_delete_cb, LV_EVENT_DELETE, name);
			}

			lv_obj_t * label = lv_label_create(item);
			if (too_big) {
				// 打不开的条目: 灰字 + 前缀标注
				lv_label_set_text_fmt(label, "（过大）%s", (name != NULL) ? name : fno.fname);
				lv_obj_set_style_text_color(label, lv_color_hex(0x9E9E9E), 0);
			} else if (too_long) {
				lv_label_set_text_fmt(label, "（名过长）%s", (name != NULL) ? name : fno.fname);
				lv_obj_set_style_text_color(label, lv_color_hex(0x9E9E9E), 0);
			} else {
				lv_label_set_text(label, (name != NULL) ? name : fno.fname);
			}
			lv_obj_set_style_text_font(label, &lv_font_16, 0);
			// 名字可能超宽, 裁掉
			lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
			lv_obj_set_width(label, LIST_LABEL_W);
			lv_obj_align(label, LV_ALIGN_LEFT_MID, 6, 0);
			lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);

			y_ofs += LIST_ITEM_H + LIST_ITEM_GAP;
			shown++;
		}
		f_closedir(&dir);
	}

	if (shown == 0) {
		lv_obj_t * tip = lv_label_create(ls.box);
		lv_label_set_text(tip, tip_text);
		lv_obj_set_style_text_font(tip, &lv_font_16, 0);
		lv_obj_set_style_text_align(tip, LV_TEXT_ALIGN_CENTER, 0);
		lv_obj_set_width(tip, LIST_ITEM_W);
		lv_obj_align(tip, LV_ALIGN_TOP_MID, 0, 40);
	}
}

// 刷新页码与左右翻页箭头的显隐
static void list_update_pager(void)
{
	if (ls.label_page != NULL) {
		lv_label_set_text_fmt(ls.label_page, "%d/%d", (int)ls.page + 1, (int)ls.page_max + 1);
	}
	// 到边界的那个箭头直接收起来, 不做"禁用"态
	if (ls.btn_prev != NULL) {
		if (ls.page == 0) lv_obj_add_flag(ls.btn_prev, LV_OBJ_FLAG_HIDDEN);
		else lv_obj_clear_flag(ls.btn_prev, LV_OBJ_FLAG_HIDDEN);
	}
	if (ls.btn_next != NULL) {
		if (ls.page >= ls.page_max) lv_obj_add_flag(ls.btn_next, LV_OBJ_FLAG_HIDDEN);
		else lv_obj_clear_flag(ls.btn_next, LV_OBJ_FLAG_HIDDEN);
	}
}

// 清掉列表内容重新填当前页, 并刷新页码
static void list_refresh(void)
{
	if (ls.box == NULL) return;

	lv_obj_clean(ls.box);// 条目携带的名字由 LV_EVENT_DELETE 释放
	list_fill();
	list_update_pager();
}

lv_obj_t * List_Create(lv_obj_t * parent, const List_Cfg_t * cfg)
{
	if (cfg == NULL) return NULL;
	if (ls.panel != NULL) return ls.panel;// 同时只允许一个列表

	ls.cfg = cfg;
	ls.path_size = (uint16_t)(strlen(cfg->dir) + 1 + cfg->name_max + strlen(cfg->ext) + 1);

	ls.panel = lv_obj_create(parent);
	lv_obj_set_size(ls.panel, LIST_W, LIST_H);
	lv_obj_align(ls.panel, LV_ALIGN_TOP_MID, 0, LIST_Y);
	lv_obj_set_style_bg_color(ls.panel, lv_color_hex(0xE8E8E8), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(ls.panel, LV_OPA_COVER, LV_PART_MAIN);
	lv_obj_set_style_pad_all(ls.panel, 0, LV_PART_MAIN);
	lv_obj_clear_flag(ls.panel, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(ls.panel, LV_OBJ_FLAG_CLICKABLE);// 挡住下层点击

	if (cfg->framed) {
		// 浮层用: 压在下层内容上, 需要一圈黑框把它分出来
		lv_obj_set_style_radius(ls.panel, 6, LV_PART_MAIN);
		lv_obj_set_style_border_width(ls.panel, 2, LV_PART_MAIN);
		lv_obj_set_style_border_color(ls.panel, lv_color_black(), LV_PART_MAIN);
	} else {
		// 页面内容区用: 本身就占满内容区, 不要边框
		lv_obj_set_style_border_width(ls.panel, 0, LV_PART_MAIN);
	}

	// 顶栏: 上一页 / 页码 / 下一页 / [右上角按钮]
	ls.btn_prev = List_Create_Btn(ls.panel, "<", 30, 24);
	lv_obj_align(ls.btn_prev, LV_ALIGN_TOP_LEFT, 10, 2);
	lv_obj_add_event_cb(ls.btn_prev, list_prev_page_cb, LV_EVENT_CLICKED, NULL);

	ls.label_page = lv_label_create(ls.panel);
	lv_label_set_text(ls.label_page, "1/1");
	lv_obj_set_style_text_font(ls.label_page, &lv_font_16, 0);
	lv_obj_set_style_text_align(ls.label_page, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(ls.label_page, 58);
	lv_obj_align(ls.label_page, LV_ALIGN_TOP_LEFT, 42, 7);

	ls.btn_next = List_Create_Btn(ls.panel, ">", 30, 24);
	lv_obj_align(ls.btn_next, LV_ALIGN_TOP_LEFT, 102, 2);
	lv_obj_add_event_cb(ls.btn_next, list_next_page_cb, LV_EVENT_CLICKED, NULL);

	if (cfg->act_text != NULL) {
		lv_obj_t * btn_act = List_Create_Btn(ls.panel, cfg->act_text, 56, 24);
		lv_obj_align(btn_act, LV_ALIGN_TOP_RIGHT, -10, 2);
		lv_obj_add_event_cb(btn_act, list_act_cb, LV_EVENT_CLICKED, NULL);
	}

	ls.box = lv_obj_create(ls.panel);
	lv_obj_set_size(ls.box, LIST_ITEM_W + 4, LIST_H - LIST_BAR_H);
	lv_obj_align(ls.box, LV_ALIGN_TOP_MID, 0, LIST_BAR_H);
	lv_obj_set_style_bg_opa(ls.box, LV_OPA_TRANSP, LV_PART_MAIN);
	lv_obj_set_style_border_width(ls.box, 0, LV_PART_MAIN);
	lv_obj_set_style_pad_all(ls.box, 0, LV_PART_MAIN);
	lv_obj_set_scroll_dir(ls.box, LV_DIR_VER);

	// 先数出总条目数定页数, 再从第 0 页开始填
	uint32_t total = list_count();
	ls.page = 0;
	ls.page_max = (total == 0) ? 0 : (uint16_t)((total - 1) / cfg->page_size);

	list_fill();
	list_update_pager();

	return ls.panel;
}

void List_Destroy(void)
{
	List_Close_Tip_Dlg();// 弹窗不在主容器里, 一并收掉

	if (ls.panel == NULL) return;

	lv_obj_del(ls.panel);
	ls.panel = NULL;
	ls.box = NULL;
	ls.btn_prev = NULL;
	ls.btn_next = NULL;
	ls.label_page = NULL;
	ls.cfg = NULL;
	ls.page = 0;
	ls.page_max = 0;
}

// ---------------- 内部回调 ----------------

// 点某个条目: 先按长度/大小筛一遍, 通过才交给调用方
static void list_item_event_cb(lv_event_t * e)
{
	const char * name = (const char *)lv_event_get_user_data(e);
	char * path;
	FILINFO fno;

	if (name == NULL || ls.cfg == NULL) return;

	// 名字长度超出上限, 不能进
	if (strlen(name) > ls.cfg->name_max) {
		List_Open_Tip_Dlg(ls.cfg->tip_too_long);
		return;
	}

	path = malloc_bsc(ls.path_size);
	if (path == NULL) {
		List_Open_Tip_Dlg("内存不足\n无法打开该文件");
		return;
	}
	list_build_path(path, name);

	// 文件超过上限, 不能进
	if (f_stat(path, &fno) == FR_OK && fno.fsize > ls.cfg->file_max) {
		free_bsc(path);
		List_Open_Tip_Dlg(ls.cfg->tip_too_big);
		return;
	}
	free_bsc(path);

	if (ls.cfg->on_item != NULL) ls.cfg->on_item(name);
}

// 条目被删除时释放它携带的名字
static void list_item_delete_cb(lv_event_t * e)
{
	void * name = lv_event_get_user_data(e);
	if (name != NULL) lv_mem_free(name);
}

// 上一页 / 下一页
static void list_prev_page_cb(lv_event_t * e)
{
	if (ls.page == 0) return;
	ls.page--;
	list_refresh();
}

static void list_next_page_cb(lv_event_t * e)
{
	if (ls.page >= ls.page_max) return;
	ls.page++;
	list_refresh();
}

// 右上角按钮
static void list_act_cb(lv_event_t * e)
{
	if (ls.cfg != NULL && ls.cfg->on_act != NULL) ls.cfg->on_act();
}

// 关掉提示弹窗
static void list_dismiss_cb(lv_event_t * e)
{
	List_Close_Tip_Dlg();
}
