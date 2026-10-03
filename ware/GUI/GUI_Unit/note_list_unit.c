#include "lvgl.h"
#include <string.h>
#include "defines.h"
#include "malloc.h"
#include "page_manager.h"
#include "ff.h"
#include "note_list_unit.h"

// 笔记列表每页条数
#define NOTE_PAGE_SIZE 5

// 编辑组件提供的路径拼接函数
extern void Note_Build_Path(char * out, const char * name);

// 对外提供的公共界面控件构造函数 (note_edit_unit 也通过 extern 使用)
lv_obj_t * Note_Create_Btn(lv_obj_t * parent, const char * txt, int32_t w, int32_t h);
lv_obj_t * Note_Create_Dlg(int32_t y_ofs, int32_t height);

// 列表状态
typedef struct {
	lv_obj_t * cont;         // 页面主容器
	lv_obj_t * list_box;     // 笔记列表的滚动容器
	lv_obj_t * btn_prev;     // 列表上一页
	lv_obj_t * btn_next;     // 列表下一页
	lv_obj_t * label_page;   // 列表页码 "2/5"
	lv_obj_t * dlg;          // 当前弹窗
	lv_obj_t * dlg_msg;      // 提示弹窗的正文
	uint16_t   page;         // 列表当前页 (0 基)
	uint16_t   page_max;     // 列表最后一页 (0 基)
} note_list_state_t;

static note_list_state_t *ns = NULL;

// 内部函数声明
static void note_item_event_cb(lv_event_t * e);
static void note_item_delete_cb(lv_event_t * e);
static void note_new_btn_event_cb(lv_event_t * e);
static void note_prev_page_cb(lv_event_t * e);
static void note_next_page_cb(lv_event_t * e);
static void note_dismiss_cb(lv_event_t * e);
static void note_close_dlg(void);
static void note_open_tip_dlg(const char * msg);
static uint8_t note_is_note(const FILINFO * fno);
static uint32_t note_count_notes(void);
static void note_fill_list(void);
static void note_refresh_page(void);
static void note_update_pager(void);

// 建一个统一样式的小按钮
lv_obj_t * Note_Create_Btn(lv_obj_t * parent, const char * txt, int32_t w, int32_t h)
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
lv_obj_t * Note_Create_Dlg(int32_t y_ofs, int32_t height)
{
	lv_obj_t * dlg = lv_obj_create(lv_layer_top());
	lv_obj_set_size(dlg, 240, height);
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

// 关闭当前弹窗
static void note_close_dlg(void)
{
	if (ns->dlg == NULL) return;
	lv_obj_del(ns->dlg);
	ns->dlg = NULL;
	ns->dlg_msg = NULL;
}

// 通用提示弹窗: 只说明原因
static void note_open_tip_dlg(const char * msg)
{
	note_close_dlg();

	ns->dlg = Note_Create_Dlg(30, 86);

	ns->dlg_msg = lv_label_create(ns->dlg);
	lv_label_set_text(ns->dlg_msg, msg);
	lv_obj_set_style_text_font(ns->dlg_msg, &lv_font_16, 0);
	lv_obj_set_style_text_align(ns->dlg_msg, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(ns->dlg_msg, 216);
	lv_obj_align(ns->dlg_msg, LV_ALIGN_TOP_MID, 0, 10);

	lv_obj_t * btn_ok = Note_Create_Btn(ns->dlg, "知道了", 72, 26);
	lv_obj_align(btn_ok, LV_ALIGN_TOP_MID, 0, 52);
	lv_obj_add_event_cb(btn_ok, note_dismiss_cb, LV_EVENT_CLICKED, NULL);

	lv_obj_move_to_index(ns->dlg, -1);// 提到最前, 别被键盘点击层挡住
}

// 判断某个目录项是不是一篇笔记
static uint8_t note_is_note(const FILINFO * fno)
{
	const size_t ext_len = strlen(NOTE_EXT);
	size_t len;

	if (fno->fattrib & (AM_HID | AM_SYS | AM_DIR)) return 0;

	len = strlen(fno->fname);
	if (len <= ext_len) return 0;
	return (strcmp(fno->fname + len - ext_len, NOTE_EXT) == 0);
}

// 数笔记目录里的篇数
static uint32_t note_count_notes(void)
{
	DIR dir;
	FILINFO fno;
	uint32_t n = 0;

	if (f_opendir(&dir, NOTE_DIR_PATH) != FR_OK) return 0;

	while (1) {
		if (f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == 0) break;
		if (note_is_note(&fno)) n++;
	}
	f_closedir(&dir);
	return n;
}

// 把当前页的条目填进列表容器
static void note_fill_list(void)
{
	const size_t ext_len = strlen(NOTE_EXT);
	DIR dir;
	FILINFO fno;
	int32_t y_ofs = 0;
	uint32_t skip = (uint32_t)ns->page * NOTE_PAGE_SIZE;// 本页之前要跳过多少篇
	uint32_t seen = 0;// 已扫过的笔记篇数
	uint32_t shown = 0;// 本页已建出的条目数
	const char * tip_text = "还没有笔记\n点右上角「新建」写第一篇";

	FRESULT res = f_opendir(&dir, NOTE_DIR_PATH);
	if (res != FR_OK) {
		// 目录还没建出来 = 还没存过笔记; 其它错误就是卷用不了
		if (res != FR_NO_PATH) tip_text = "SD卡不可用";
	} else {
		while (shown < NOTE_PAGE_SIZE) {
			if (f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == 0) break;
			if (!note_is_note(&fno)) continue;
			if (seen++ < skip) continue;// 还在本页之前

			size_t len = strlen(fno.fname);
			uint8_t too_big = (fno.fsize > NOTE_FILE_MAX) ? 1 : 0;// 读不进来的大文件
			uint8_t too_long = ((len - ext_len) > NOTE_NAME_MAX) ? 1 : 0;// 名字超出本模块能保存的长度

			lv_obj_t * item = lv_obj_create(ns->list_box);
			lv_obj_set_size(item, 232, 26);
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
				lv_obj_add_event_cb(item, note_item_event_cb, LV_EVENT_CLICKED, name);
				lv_obj_add_event_cb(item, note_item_delete_cb, LV_EVENT_DELETE, name);
			}

			lv_obj_t * label = lv_label_create(item);
			if (too_big) {
				// 不可编辑的条目: 灰字 + 前缀标注
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
			lv_obj_set_width(label, 220);
			lv_obj_align(label, LV_ALIGN_LEFT_MID, 6, 0);
			lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);

			y_ofs += 28;
			shown++;
		}
		f_closedir(&dir);
	}

	if (shown == 0) {
		lv_obj_t * tip = lv_label_create(ns->list_box);
		lv_label_set_text(tip, tip_text);
		lv_obj_set_style_text_font(tip, &lv_font_16, 0);
		lv_obj_set_style_text_align(tip, LV_TEXT_ALIGN_CENTER, 0);
		lv_obj_set_width(tip, 232);
		lv_obj_align(tip, LV_ALIGN_TOP_MID, 0, 40);
	}
}

// 清掉列表内容重新填当前页, 并刷新页码
static void note_refresh_page(void)
{
	if (ns->list_box == NULL) return;

	lv_obj_clean(ns->list_box);// 条目携带的名字由 LV_EVENT_DELETE 释放
	note_fill_list();
	note_update_pager();
}

// 刷新页码与左右翻页箭头的显隐
static void note_update_pager(void)
{
	if (ns->label_page != NULL) {
		lv_label_set_text_fmt(ns->label_page, "%d/%d", (int)ns->page + 1, (int)ns->page_max + 1);
	}
	// 到边界的那个箭头直接收起来, 不做"禁用"态
	if (ns->btn_prev != NULL) {
		if (ns->page == 0) lv_obj_add_flag(ns->btn_prev, LV_OBJ_FLAG_HIDDEN);
		else lv_obj_clear_flag(ns->btn_prev, LV_OBJ_FLAG_HIDDEN);
	}
	if (ns->btn_next != NULL) {
		if (ns->page >= ns->page_max) lv_obj_add_flag(ns->btn_next, LV_OBJ_FLAG_HIDDEN);
		else lv_obj_clear_flag(ns->btn_next, LV_OBJ_FLAG_HIDDEN);
	}
}

void Create_Note_List_Unit(void)
{
	if (ns != NULL) return;

	ns = (note_list_state_t *)malloc_ccm(sizeof(note_list_state_t));
	if (!ns) return;
	memset(ns, 0, sizeof(note_list_state_t));

	ns->cont = lv_obj_create(lv_scr_act());
	lv_obj_set_size(ns->cont, 240, 180);
	lv_obj_center(ns->cont);
	lv_obj_clear_flag(ns->cont, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_border_width(ns->cont, 0, LV_PART_MAIN);
	lv_obj_set_style_pad_all(ns->cont, 0, LV_PART_MAIN);
	lv_obj_set_style_bg_color(ns->cont, lv_color_hex(0xE8E8E8), LV_PART_MAIN);

	// 顶栏: 上一页 / 页码 / 下一页 / 新建
	ns->btn_prev = Note_Create_Btn(ns->cont, "<", 30, 24);
	lv_obj_align(ns->btn_prev, LV_ALIGN_TOP_LEFT, 10, 2);
	lv_obj_add_event_cb(ns->btn_prev, note_prev_page_cb, LV_EVENT_CLICKED, NULL);

	ns->label_page = lv_label_create(ns->cont);
	lv_label_set_text(ns->label_page, "1/1");
	lv_obj_set_style_text_font(ns->label_page, &lv_font_16, 0);
	lv_obj_set_style_text_align(ns->label_page, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(ns->label_page, 58);
	lv_obj_align(ns->label_page, LV_ALIGN_TOP_LEFT, 42, 7);

	ns->btn_next = Note_Create_Btn(ns->cont, ">", 30, 24);
	lv_obj_align(ns->btn_next, LV_ALIGN_TOP_LEFT, 102, 2);
	lv_obj_add_event_cb(ns->btn_next, note_next_page_cb, LV_EVENT_CLICKED, NULL);

	lv_obj_t * btn_new = Note_Create_Btn(ns->cont, "新建", 56, 24);
	lv_obj_align(btn_new, LV_ALIGN_TOP_RIGHT, -10, 2);
	lv_obj_add_event_cb(btn_new, note_new_btn_event_cb, LV_EVENT_CLICKED, NULL);

	ns->list_box = lv_obj_create(ns->cont);
	lv_obj_set_size(ns->list_box, 236, 146);
	lv_obj_align(ns->list_box, LV_ALIGN_TOP_MID, 0, 32);
	lv_obj_set_style_bg_opa(ns->list_box, LV_OPA_TRANSP, LV_PART_MAIN);
	lv_obj_set_style_border_width(ns->list_box, 0, LV_PART_MAIN);
	lv_obj_set_style_pad_all(ns->list_box, 0, LV_PART_MAIN);
	lv_obj_set_scroll_dir(ns->list_box, LV_DIR_VER);

	// 先数出总篇数定页数, 再从第 0 页开始填
	uint32_t total = note_count_notes();
	ns->page = 0;
	ns->page_max = (total == 0) ? 0 : (uint16_t)((total - 1) / NOTE_PAGE_SIZE);

	note_fill_list();
	note_update_pager();
}

void Update_Note_List_Unit(void)
{
	// 列表没有后台动作要收尾, 保留空实现与页面接口对齐
}

void Remove_Note_List_Unit(void)
{
	if (ns == NULL) return;

	note_close_dlg();// 弹窗不在主容器里, 手动删

	if (ns->cont != NULL) {
		lv_obj_del(ns->cont);
	}

	free_ccm(ns);
	ns = NULL;
}

// ---------------- 内部回调 ----------------

// 点列表里的某篇笔记: 把名字交给公共区, 切到编辑页
static void note_item_event_cb(lv_event_t * e)
{
	const char * name = (const char *)lv_event_get_user_data(e);

	if (name == NULL) return;

	// 名字长度超出上限, 不进编辑页
	if (strlen(name) > NOTE_NAME_MAX) {
		note_open_tip_dlg("文件名过长\n无法编辑该笔记");
		return;
	}

	// 文件超过上限, 不进编辑页
	char * path = malloc_bsc(NOTE_PATH_MAX);
	if (path == NULL) {
		note_open_tip_dlg("内存不足\n无法打开该笔记");
		return;
	}
	FILINFO fno;
	Note_Build_Path(path, name);
	if (f_stat(path, &fno) == FR_OK && fno.fsize > NOTE_FILE_MAX) {
		free_bsc(path);
		note_open_tip_dlg("文件过大\n只能编辑 4 KB 以内的笔记");
		return;
	}
	free_bsc(path);

	// 笔记名经由入口参数交给编辑页 (与文件管理器传目录路径同一机制)
	Page_Request_Switch(PAGE_NOTE_EDIT, name);
}

// 列表上一页 / 下一页
static void note_prev_page_cb(lv_event_t * e)
{
	if (ns->page == 0) return;
	ns->page--;
	note_refresh_page();
}

static void note_next_page_cb(lv_event_t * e)
{
	if (ns->page >= ns->page_max) return;
	ns->page++;
	note_refresh_page();
}

// 关闭弹窗
static void note_dismiss_cb(lv_event_t * e)
{
	note_close_dlg();
}

// 列表条目被删除时释放它携带的名字
static void note_item_delete_cb(lv_event_t * e)
{
	void * name = lv_event_get_user_data(e);
	if (name != NULL) lv_mem_free(name);
}

// 点新建按钮: 起一篇空白的笔记
static void note_new_btn_event_cb(lv_event_t * e)
{
	// 空笔记名表示新建
	Page_Request_Switch(PAGE_NOTE_EDIT, "");
}
