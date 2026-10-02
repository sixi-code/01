#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include "variables.h"
#include "defines.h"
#include "keyboard.h"
#include "malloc.h"
#include "page_manager.h"
#include "ff.h"

// 笔记在 TF 卡上的存放位置
#define NOTE_DIR_PATH  "0:/Note"   // 笔记目录
#define NOTE_EXT       ".txt"      // 笔记后缀 (用它区分哪些文件是笔记)
#define NOTE_NAME_MAX  48          // 笔记名最大字节数 (不含后缀)
#define NOTE_PATH_MAX  80          // 完整路径缓冲区大小
#define NOTE_MAX_CHAR  1024        // 正文最大字符数 (与文本框上限一致)
#define NOTE_MAX_BYTE  (NOTE_MAX_CHAR * 3) // 正文最大字节数 (UTF-8 下一个汉字占 3 字节)
#define NOTE_BOM_LEN   3           // UTF-8 BOM 长度
// 笔记文件字节上限: BOM + 正文 + 每个换行多出来的 '\r'
#define NOTE_FILE_MAX  (NOTE_BOM_LEN + NOTE_MAX_BYTE + NOTE_MAX_CHAR)
#define NOTE_PAGE_SIZE 5           // 笔记列表每页条数

// 写文件时带上的 UTF-8 BOM
static const uint8_t NOTE_BOM[3] = {0xEF, 0xBB, 0xBF};

// 视图
enum {
	NOTE_VIEW_LIST = 0, // 笔记列表
	NOTE_VIEW_EDIT      // 编辑单篇笔记
};

// 延迟到 Update 里执行的视图切换请求
enum {
	NOTE_GO_NONE = 0,   // 无请求
	NOTE_GO_LIST,       // 切到列表
	NOTE_GO_EDIT        // 切到编辑 (用 ns->name, 空串表示新建)
};

// 「还没保存」确认框当前是在为哪件事做准备
enum {
	NOTE_ASK_NONE = 0,  // 无
	NOTE_ASK_LEAVE_PAGE,// 要离开本页面
	NOTE_ASK_BACK_LIST  // 要回到笔记列表
};

// 确认框的三个选择
enum {
	NOTE_CHOICE_SAVE = 0, // 保存
	NOTE_CHOICE_DROP,     // 不保存
	NOTE_CHOICE_STAY      // 取消
};

// 笔记名校验结果
enum {
	NOTE_NAME_OK = 0,   // 通过
	NOTE_NAME_BAD,      // 空 / 含非法字符 / 过长
	NOTE_NAME_EXIST     // 已被别的笔记占用
};

// --- 状态结构体 ---
typedef struct {
	lv_obj_t * cont;         // 页面主容器
	lv_obj_t * list_view;    // 列表视图
	lv_obj_t * edit_view;    // 编辑视图
	lv_obj_t * list_box;     // 笔记列表的滚动容器
	lv_obj_t * btn_prev;     // 列表上一页
	lv_obj_t * btn_next;     // 列表下一页
	lv_obj_t * label_page;   // 列表页码 "2/5"
	lv_obj_t * ta_note;      // 正文文本框
	lv_obj_t * label_name;   // 顶栏右侧的当前笔记名
	lv_obj_t * dlg;          // 当前弹窗
	lv_obj_t * dlg_ta;       // 起名弹窗的名称输入框
	lv_obj_t * dlg_msg;      // 起名弹窗的标题兼错误提示 / 确认框的正文
	lv_style_t style_note_bg;// 正文文本框样式
	char       name[NOTE_NAME_MAX + 1]; // 当前笔记名 (不含后缀), 空串表示还没起名
	uint16_t   page;         // 列表当前页 (0 基)
	uint16_t   page_max;     // 列表最后一页 (0 基)
	uint8_t    dirty;        // 正文是否被改过
	uint8_t    view;         // 当前视图 NOTE_VIEW_*
	uint8_t    go;           // 待执行的视图切换 NOTE_GO_*
	uint8_t    ask;          // 确认框正在等哪件事 NOTE_ASK_*
	uint8_t    exit_ok;      // 是否放行页面切换
	uint8_t    save_pending; // 写盘已交给后台任务, 正在等它完成
} note_state_t;

static note_state_t *ns = NULL;

// 内部函数声明
static void note_ta_event_cb(lv_event_t * e);
static void note_list_btn_event_cb(lv_event_t * e);
static void note_new_btn_event_cb(lv_event_t * e);
static void note_item_event_cb(lv_event_t * e);
static void note_item_delete_cb(lv_event_t * e);
static void note_prev_page_cb(lv_event_t * e);
static void note_next_page_cb(lv_event_t * e);
static void note_dismiss_cb(lv_event_t * e);
static void note_name_dlg_ok_cb(lv_event_t * e);
static void note_name_dlg_cancel_cb(lv_event_t * e);
static void note_ask_dlg_event_cb(lv_event_t * e);
static lv_obj_t * note_create_btn(lv_obj_t * parent, const char * txt, int32_t w, int32_t h);
static lv_obj_t * note_create_dlg(int32_t y_ofs, int32_t height);
static void note_close_dlg(void);
static void note_dlg_err(const char * msg);
static void note_update_name_label(void);
static void note_abort_switch(void);
static void note_save_ok(void);
static void note_save_err(uint8_t res);
static void note_show_list(void);
static void note_show_edit(void);
static void note_fill_list(void);
static void note_refresh_page(void);
static void note_update_pager(void);
static uint8_t note_is_note(const FILINFO * fno);
static uint32_t note_count_notes(void);
static void note_open_name_dlg(void);
static void note_open_ask_dlg(void);
static void note_open_tip_dlg(const char * msg);
static void note_trim(char * s);
static uint8_t note_check_name(const char * name);
static void note_build_path(char * out, const char * name);
static uint8_t note_fr_to_result(FRESULT res);
static uint8_t note_submit_save(const char * name);
static void note_load_from_file(void);

// 辅助函数：初始化正文文本框的样式
static void init_custom_styles(void)
{
	lv_style_init(&ns->style_note_bg);
	lv_style_set_radius(&ns->style_note_bg, 4);
	lv_style_set_border_width(&ns->style_note_bg, 1);
	lv_style_set_border_color(&ns->style_note_bg, lv_color_black());
	lv_style_set_bg_color(&ns->style_note_bg, lv_color_white());
	lv_style_set_bg_opa(&ns->style_note_bg, LV_OPA_100);
	lv_style_set_pad_all(&ns->style_note_bg, 8);
	lv_style_set_text_color(&ns->style_note_bg, lv_color_black());
}

// 建一个统一样式的小按钮
static lv_obj_t * note_create_btn(lv_obj_t * parent, const char * txt, int32_t w, int32_t h)
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
static lv_obj_t * note_create_dlg(int32_t y_ofs, int32_t height)
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

	Close_Keyboard();// 先解绑键盘
	lv_obj_del(ns->dlg);
	ns->dlg = NULL;
	ns->dlg_ta = NULL;
	ns->dlg_msg = NULL;
}

// 在起名弹窗上提示一条错误
static void note_dlg_err(const char * msg)
{
	if (ns->dlg_msg == NULL) return;
	lv_obj_set_style_text_color(ns->dlg_msg, lv_color_hex(0xD32F2F), 0);
	lv_label_set_text(ns->dlg_msg, msg);
}

// 刷新顶栏右侧的笔记名
static void note_update_name_label(void)
{
	if (ns->label_name == NULL) return;
	lv_label_set_text(ns->label_name, (ns->name[0] != '\0') ? ns->name : "未命名");
}

// 取消本次离开: 把切换目标改回当前页
static void note_abort_switch(void)
{
	Page_Request_Switch(Page_Get_Current());
}

// 写盘成功后的收尾: 按 ask 决定离开页面还是回到列表
static void note_save_ok(void)
{
	uint8_t ask = ns->ask;

	ns->dirty = 0;
	ns->ask = NOTE_ASK_NONE;

	if (ask == NOTE_ASK_LEAVE_PAGE) ns->exit_ok = 1;// 放行本次页面切换
	else if (ask == NOTE_ASK_BACK_LIST) ns->go = NOTE_GO_LIST;// 回到列表

	note_close_dlg();
}

// 写盘没成: 把原因贴在当前那个弹窗上
static void note_save_err(uint8_t res)
{
	if (ns->dlg_ta != NULL) {// 起名弹窗还开着
		note_dlg_err((res == FILEOP_RES_NO_SD) ? "SD卡不可用" : "写入失败");
	}
	else if (ns->dlg_msg != NULL) {// 「还没保存」确认框还开着
		lv_obj_set_style_text_color(ns->dlg_msg, lv_color_hex(0xD32F2F), 0);
		lv_label_set_text(ns->dlg_msg,
			(res == FILEOP_RES_NO_SD) ? "SD卡不可用\n点「不保存」可丢弃改动"
									  : "写入失败\n点「不保存」可丢弃改动");
	}
}

// ---------------- TF 卡读写 ----------------

// 去掉字符串首尾的空格
static void note_trim(char * s)
{
	size_t len = strlen(s);
	size_t i = 0;
	while (i < len && s[i] == ' ') i++;
	if (i > 0) memmove(s, s + i, len - i + 1);

	len = strlen(s);
	while (len > 0 && s[len - 1] == ' ') s[--len] = '\0';
}

// 校验笔记名, 返回 NOTE_NAME_OK 或 NOTE_NAME_BAD
static uint8_t note_check_name(const char * name)
{
	if (name == NULL || name[0] == '\0') return NOTE_NAME_BAD;
	if (strlen(name) > NOTE_NAME_MAX) return NOTE_NAME_BAD;

	// 文件名里不能出现的字符, 另外把控制字符也挡掉
	const char * bad = "\"*/:<>?\\|";
	for (const char * p = name; *p; p++) {
		if ((uint8_t)*p < 0x20) return NOTE_NAME_BAD;
		if (strchr(bad, (int)(uint8_t)*p) != NULL) return NOTE_NAME_BAD;
	}
	return NOTE_NAME_OK;
}

// 拼出某个笔记名对应的完整路径
static void note_build_path(char * out, const char * name)
{
	sprintf(out, "%s/%s%s", NOTE_DIR_PATH, name, NOTE_EXT);
}

// 把 FatFs 返回码归类为 FILEOP_RES_*
static uint8_t note_fr_to_result(FRESULT res)
{
	if (res == FR_OK) return FILEOP_RES_OK;
	if (res == FR_NOT_READY || res == FR_INVALID_DRIVE || res == FR_NO_FILESYSTEM) {
		return FILEOP_RES_NO_SD;
	}
	return FILEOP_RES_FAIL;
}

// 把 text 写成标准 Windows 文本文件(UTF-8 + BOM + CRLF), 返回 FILEOP_RES_*
// 本函数由后台文件操作任务调用
int do_write_text(const char * path, const char * text)
{
	if (path == NULL || text == NULL) return FILEOP_RES_FAIL;

	f_mkdir(NOTE_DIR_PATH);// 创建笔记目录, 已存在时忽略错误

	// 目标文件超过上限时不截断重写
	FILINFO fno;
	if (f_stat(path, &fno) == FR_OK && fno.fsize > NOTE_FILE_MAX) return FILEOP_RES_FAIL;

	FIL file;
	FRESULT res = f_open(&file, path, FA_WRITE | FA_CREATE_ALWAYS);
	if (res != FR_OK) return note_fr_to_result(res);

	uint8_t result = FILEOP_RES_OK;
	UINT bw = 0;

	// 先写 BOM
	res = f_write(&file, NOTE_BOM, sizeof(NOTE_BOM), &bw);
	if (res != FR_OK || bw != sizeof(NOTE_BOM)) result = FILEOP_RES_FAIL;

	// 分段写正文, 把 '\n' 落成 "\r\n"
	const char * p = text;
	while (result == FILEOP_RES_OK && *p != '\0') {
		const char * nl = strchr(p, '\n');
		size_t seg = (nl != NULL) ? (size_t)(nl - p) : strlen(p);// 本行不含换行的长度

		if (seg > 0) {
			res = f_write(&file, p, seg, &bw);
			if (res != FR_OK || bw != seg) result = FILEOP_RES_FAIL;
		}
		if (nl == NULL) break;

		res = f_write(&file, "\r\n", 2, &bw);
		if (res != FR_OK || bw != 2) result = FILEOP_RES_FAIL;

		p = nl + 1;
	}

	f_close(&file);
	return result;
}

// 把"写这篇笔记"交给后台文件操作任务, 返回 FILEOP_RES_* (OK = 已排队)
static uint8_t note_submit_save(const char * name)
{
	if (ns == NULL || ns->ta_note == NULL) return FILEOP_RES_FAIL;
	if (g_file_op_busy) return FILEOP_RES_FAIL;// 后台正忙
	if (FileOp_Task_handler == NULL) return FILEOP_RES_FAIL;// 任务还没起来
	// 卡没插时那个任务还在等 g_TFcard_inited, 根本不会进主循环;
	// 这时候提交会让 busy 永远不落, 页面就卡死了 ⇒ 直接判"卡不可用"
	if (!g_TFcard_inited) return FILEOP_RES_NO_SD;

	const char * txt = lv_textarea_get_text(ns->ta_note);
	if (txt == NULL) txt = "";

	char path[NOTE_PATH_MAX];
	note_build_path(path, name);

	// 路径与正文各复制一份交给任务, 任务用完自己释放
	char * src = malloc_bsc(strlen(path) + 1);
	char * dst = malloc_bsc(strlen(txt) + 1);
	if (src == NULL || dst == NULL) {
		if (src != NULL) free_bsc(src);
		if (dst != NULL) free_bsc(dst);
		return FILEOP_RES_FAIL;
	}
	strcpy(src, path);
	strcpy(dst, txt);

	// 收掉键盘: 写盘期间不能再改正文(改动不在这次快照里, 写完之后就会被 dirty=0 抹掉)
	Close_Keyboard();

	g_async_src    = src;
	g_async_dst    = dst;
	g_file_op_cmd  = 3;// 写文本文件
	g_file_op_busy = 1;
	g_file_op_done = 0;
	xTaskNotifyGive(FileOp_Task_handler);

	ns->save_pending = 1;
	return FILEOP_RES_OK;
}

// 把当前笔记的正文读回文本框
static void note_load_from_file(void)
{
	if (ns == NULL || ns->ta_note == NULL) return;
	if (ns->name[0] == '\0') return;// 新建的笔记没有文件可读

	char path[NOTE_PATH_MAX];
	note_build_path(path, ns->name);

	FIL file;
	FRESULT res = f_open(&file, path, FA_READ);
	if (res != FR_OK) return;// 文件不存在 / 卷不可用时保持空白

	// 过大的文件不读
	if (f_size(&file) > NOTE_FILE_MAX) {
		f_close(&file);
		return;
	}

	char * buf = malloc_bsc(NOTE_FILE_MAX + 1);// 按文件上限分配读取缓冲区
	if (buf == NULL) {
		f_close(&file);
		return;
	}

	uint32_t total = 0;// 已读出的字节数
	while (total < NOTE_FILE_MAX) {// 循环读取, 兼容 FatFs 的单次短读
		UINT br = 0;
		res = f_read(&file, buf + total, NOTE_FILE_MAX - total, &br);
		if (res != FR_OK || br == 0) break;
		total += br;
	}
	f_close(&file);

	buf[total] = '\0';

	// 跳过 UTF-8 BOM, 并去掉 Windows 换行里的 '\r'
	uint32_t i = 0;
	if (total >= NOTE_BOM_LEN && (uint8_t)buf[0] == NOTE_BOM[0] &&
		(uint8_t)buf[1] == NOTE_BOM[1] && (uint8_t)buf[2] == NOTE_BOM[2]) {
		i = NOTE_BOM_LEN;
	}
	uint32_t w = 0;
	for (; i < total; i++) {
		if (buf[i] != '\r') buf[w++] = buf[i];
	}
	buf[w] = '\0';

	lv_textarea_set_text(ns->ta_note, buf);
	free_bsc(buf);
}

// ---------------- 两个视图 ----------------

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

// 切到列表视图
static void note_show_list(void)
{
	Close_Keyboard();// 先解绑键盘
	note_close_dlg();

	if (ns->list_view) { lv_obj_del(ns->list_view); ns->list_view = NULL; }
	if (ns->edit_view) {
		lv_obj_del(ns->edit_view);
		ns->edit_view = NULL;
		ns->ta_note = NULL;
		ns->label_name = NULL;
	}
	ns->list_box = NULL;
	ns->btn_prev = NULL;
	ns->btn_next = NULL;
	ns->label_page = NULL;

	ns->view = NOTE_VIEW_LIST;
	ns->name[0] = '\0';
	ns->dirty = 0;
	ns->go = NOTE_GO_NONE;
	ns->ask = NOTE_ASK_NONE;
	ns->exit_ok = 0;

	ns->list_view = lv_obj_create(ns->cont);
	lv_obj_set_size(ns->list_view, 240, 180);
	lv_obj_set_pos(ns->list_view, 0, 0);
	lv_obj_set_style_bg_opa(ns->list_view, LV_OPA_TRANSP, LV_PART_MAIN);
	lv_obj_set_style_border_width(ns->list_view, 0, LV_PART_MAIN);
	lv_obj_set_style_pad_all(ns->list_view, 0, LV_PART_MAIN);
	lv_obj_clear_flag(ns->list_view, LV_OBJ_FLAG_SCROLLABLE);

	// 顶栏: 上一页 / 页码 / 下一页 / 新建
	ns->btn_prev = note_create_btn(ns->list_view, "<", 30, 24);
	lv_obj_align(ns->btn_prev, LV_ALIGN_TOP_LEFT, 10, 2);
	lv_obj_add_event_cb(ns->btn_prev, note_prev_page_cb, LV_EVENT_CLICKED, NULL);

	ns->label_page = lv_label_create(ns->list_view);
	lv_label_set_text(ns->label_page, "1/1");
	lv_obj_set_style_text_font(ns->label_page, &lv_font_16, 0);
	lv_obj_set_style_text_align(ns->label_page, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(ns->label_page, 58);
	lv_obj_align(ns->label_page, LV_ALIGN_TOP_LEFT, 42, 7);

	ns->btn_next = note_create_btn(ns->list_view, ">", 30, 24);
	lv_obj_align(ns->btn_next, LV_ALIGN_TOP_LEFT, 102, 2);
	lv_obj_add_event_cb(ns->btn_next, note_next_page_cb, LV_EVENT_CLICKED, NULL);

	lv_obj_t * btn_new = note_create_btn(ns->list_view, "新建", 56, 24);
	lv_obj_align(btn_new, LV_ALIGN_TOP_RIGHT, -10, 2);
	lv_obj_add_event_cb(btn_new, note_new_btn_event_cb, LV_EVENT_CLICKED, NULL);

	ns->list_box = lv_obj_create(ns->list_view);
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

// 切到编辑视图; ns->name 为空表示新建, 非空表示打开已有笔记
static void note_show_edit(void)
{
	Close_Keyboard();// 先解绑键盘
	note_close_dlg();

	if (ns->list_view) { lv_obj_del(ns->list_view); ns->list_view = NULL; }
	if (ns->edit_view) { lv_obj_del(ns->edit_view); ns->edit_view = NULL; }
	ns->list_box = NULL;
	ns->btn_prev = NULL;
	ns->btn_next = NULL;
	ns->label_page = NULL;

	ns->view = NOTE_VIEW_EDIT;
	ns->dirty = 0;
	ns->go = NOTE_GO_NONE;
	ns->ask = NOTE_ASK_NONE;
	ns->exit_ok = 0;

	ns->edit_view = lv_obj_create(ns->cont);
	lv_obj_set_size(ns->edit_view, 240, 180);
	lv_obj_set_pos(ns->edit_view, 0, 0);
	lv_obj_set_style_bg_opa(ns->edit_view, LV_OPA_TRANSP, LV_PART_MAIN);
	lv_obj_set_style_border_width(ns->edit_view, 0, LV_PART_MAIN);
	lv_obj_set_style_pad_all(ns->edit_view, 0, LV_PART_MAIN);
	lv_obj_clear_flag(ns->edit_view, LV_OBJ_FLAG_SCROLLABLE);

	// 顶栏: 列表 / 当前笔记名
	lv_obj_t * btn_list = note_create_btn(ns->edit_view, "列表", 56, 24);
	lv_obj_align(btn_list, LV_ALIGN_TOP_LEFT, 10, 2);
	lv_obj_add_event_cb(btn_list, note_list_btn_event_cb, LV_EVENT_CLICKED, NULL);

	ns->label_name = lv_label_create(ns->edit_view);
	lv_label_set_text(ns->label_name, "未命名");
	lv_obj_set_style_text_font(ns->label_name, &lv_font_16, 0);
	lv_label_set_long_mode(ns->label_name, LV_LABEL_LONG_CLIP);
	lv_obj_set_width(ns->label_name, 158);
	lv_obj_set_style_text_align(ns->label_name, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_align(ns->label_name, LV_ALIGN_TOP_LEFT, 72, 7);

	// 正文
	ns->ta_note = lv_textarea_create(ns->edit_view);
	lv_obj_set_size(ns->ta_note, 220, 146);
	lv_obj_align(ns->ta_note, LV_ALIGN_TOP_MID, 0, 32);

	lv_textarea_set_placeholder_text(ns->ta_note, "在这里输入笔记（支持英文+数字）...");
	lv_textarea_set_one_line(ns->ta_note, false);
	lv_textarea_set_max_length(ns->ta_note, NOTE_MAX_CHAR);
	lv_textarea_set_cursor_click_pos(ns->ta_note, true);

	lv_obj_add_style(ns->ta_note, &ns->style_note_bg, LV_PART_MAIN);
	lv_obj_add_event_cb(ns->ta_note, note_ta_event_cb, LV_EVENT_ALL, NULL);

	note_load_from_file();// 打开已有笔记时读回内容
	ns->dirty = 0;// 读回来的内容不算改动
	note_update_name_label();

	lv_obj_add_state(ns->ta_note, LV_STATE_FOCUSED);
}

// ---------------- 对外接口 ----------------

void Create_Note_Unit(void)
{
	if (ns != NULL) return;

	ns = (note_state_t *)malloc_ccm(sizeof(note_state_t));
	if (!ns) return;
	memset(ns, 0, sizeof(note_state_t));

	init_custom_styles();

	ns->cont = lv_obj_create(lv_scr_act());
	lv_obj_set_size(ns->cont, 240, 180);
	lv_obj_center(ns->cont);
	lv_obj_clear_flag(ns->cont, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_border_width(ns->cont, 0, LV_PART_MAIN);
	lv_obj_set_style_pad_all(ns->cont, 0, LV_PART_MAIN);
	lv_obj_set_style_bg_color(ns->cont, lv_color_hex(0xE8E8E8), LV_PART_MAIN);

	note_show_list();// 进页面先显示笔记列表
}

void Update_Note_Unit(void)
{
	if (ns == NULL) return;

	// 后台写盘完成了就收尾(写盘不在 LVGL 任务里做)
	if (ns->save_pending && !g_file_op_busy) {
		uint8_t res = g_file_op_result;
		g_file_op_done = 0;
		ns->save_pending = 0;
		if (res == FILEOP_RES_OK) note_save_ok();
		else note_save_err(res);
		return;
	}

	// 视图切换统一在这里做
	if (ns->go == NOTE_GO_LIST) {
		note_show_list();
		return;
	}
	if (ns->go == NOTE_GO_EDIT) {
		note_show_edit();
		return;
	}

	Update_Keyboard();
}

void Remove_Note_Unit(void)
{
	if (ns == NULL) return;

	note_close_dlg();// 弹窗不在主容器里, 手动删
	Close_Keyboard();// 解绑键盘

	if (ns->cont != NULL) {
		lv_obj_del(ns->cont);
	}

	free_ccm(ns);
	ns = NULL;
}

// 页面切换前的询问: 有未保存改动就弹确认框并拦下切换
bool Note_Unit_Can_Exit(void)
{
	if (ns == NULL) return true;
	if (ns->view != NOTE_VIEW_EDIT) return true;// 列表视图没有会丢的内容
	if (!ns->dirty || ns->exit_ok) return true;

	if (ns->dlg == NULL) {// 还没弹过
		ns->ask = NOTE_ASK_LEAVE_PAGE;
		note_open_ask_dlg();
	} else if (ns->ask == NOTE_ASK_BACK_LIST) {
		// 弹窗已开着且是为回列表准备的, 改为按离开本页处理
		ns->ask = NOTE_ASK_LEAVE_PAGE;
	}
	return false;
}

// ---------------- 内部回调 ----------------

static void note_ta_event_cb(lv_event_t * e)
{
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t * ta = lv_event_get_target(e);

	if (code == LV_EVENT_FOCUSED) {
		Create_Keyboard(ta);
		// 弹窗还开着就重新提到最前
		if (ns->dlg != NULL) lv_obj_move_to_index(ns->dlg, -1);
	}
	else if (code == LV_EVENT_VALUE_CHANGED) {
		ns->dirty = 1;// 正文有改动
	}
}

// 点列表按钮: 有改动先弹确认框
static void note_list_btn_event_cb(lv_event_t * e)
{
	if (ns->dirty) {
		ns->ask = NOTE_ASK_BACK_LIST;
		note_open_ask_dlg();
		return;
	}
	ns->go = NOTE_GO_LIST;
}

// 点新建按钮: 起一篇空白的笔记
static void note_new_btn_event_cb(lv_event_t * e)
{
	ns->name[0] = '\0';
	ns->go = NOTE_GO_EDIT;
}

// 点列表里的某篇笔记: 把名字拷进状态, 延迟换视图
static void note_item_event_cb(lv_event_t * e)
{
	const char * name = (const char *)lv_event_get_user_data(e);

	if (name == NULL) return;

	// 名字长度超出上限, 不进编辑页
	if (strlen(name) > NOTE_NAME_MAX) {
		note_open_tip_dlg("文件名过长\n只能编辑 16 个字符内的笔记");
		return;
	}

	// 文件超过上限, 不进编辑页
	char path[NOTE_PATH_MAX];
	FILINFO fno;
	note_build_path(path, name);
	if (f_stat(path, &fno) == FR_OK && fno.fsize > NOTE_FILE_MAX) {
		note_open_tip_dlg("文件过大\n只能编辑 4 KB 以内的笔记");
		return;
	}

	strncpy(ns->name, name, NOTE_NAME_MAX);
	ns->name[NOTE_NAME_MAX] = '\0';
	ns->go = NOTE_GO_EDIT;
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

// 起名弹窗点确定: 校验名称 + 查重, 通过才写盘
static void note_name_dlg_ok_cb(lv_event_t * e)
{
	char name[NOTE_NAME_MAX + 1];
	const char * src;

	if (ns->dlg_ta == NULL) return;
	if (ns->save_pending) return;// 写盘还在后台跑, 先不接受按键

	// 名称按字节数限长, 超了直接报错
	src = lv_textarea_get_text(ns->dlg_ta);
	if (src == NULL || strlen(src) > NOTE_NAME_MAX) {
		note_dlg_err("名称太长");
		return;
	}
	strcpy(name, src);
	note_trim(name);

	if (note_check_name(name) != NOTE_NAME_OK) {
		note_dlg_err((name[0] == '\0') ? "请先输入名称" : "名称含非法字符");
		return;
	}

	// 查重: 同名且不是当前这篇就报错
	char path[NOTE_PATH_MAX];
	FILINFO fno;
	note_build_path(path, name);
	if (f_stat(path, &fno) == FR_OK && strcmp(name, ns->name) != 0) {
		note_dlg_err("名称已存在");
		return;
	}

	strncpy(ns->name, name, NOTE_NAME_MAX);
	ns->name[NOTE_NAME_MAX] = '\0';
	note_update_name_label();

	// 写盘交给后台任务, 完成后在 Update 里收尾
	if (note_submit_save(ns->name) != FILEOP_RES_OK) {
		note_save_err(FILEOP_RES_FAIL);
	}
}

// 起名弹窗点取消: 放弃保存并取消这次离开
static void note_name_dlg_cancel_cb(lv_event_t * e)
{
	uint8_t ask = ns->ask;
	ns->ask = NOTE_ASK_NONE;
	note_close_dlg();

	if (ask == NOTE_ASK_LEAVE_PAGE) note_abort_switch();
}

// 「还没保存」确认框的三个按钮
static void note_ask_dlg_event_cb(lv_event_t * e)
{
	int choice = (int)(intptr_t)lv_event_get_user_data(e);
	uint8_t ask = ns->ask;

	if (ns->save_pending) return;// 写盘还在后台跑, 先不接受按键

	if (choice == NOTE_CHOICE_STAY) {
		ns->ask = NOTE_ASK_NONE;
		note_close_dlg();
		if (ask == NOTE_ASK_LEAVE_PAGE) note_abort_switch();// 取消离开, 留在本页
		return;
	}

	if (choice == NOTE_CHOICE_DROP) {
		ns->ask = NOTE_ASK_NONE;
		ns->dirty = 0;// 丢弃改动
		note_close_dlg();
		if (ask == NOTE_ASK_LEAVE_PAGE) ns->exit_ok = 1;// 放行页面切换
		else ns->go = NOTE_GO_LIST;
		return;
	}

	// 选保存: 已有名字就交后台写, 没名字先去起名
	if (ns->name[0] != '\0') {
		if (note_submit_save(ns->name) != FILEOP_RES_OK) {
			note_save_err(FILEOP_RES_FAIL);
		}
		return;
	}
	note_open_name_dlg();
}

// ---------------- 弹窗 ----------------

// 起名弹窗: 确认后按这个名称保存
static void note_open_name_dlg(void)
{
	note_close_dlg();

	ns->dlg = note_create_dlg(30, 86);

	// 平时显示标题, 出错时显示红色提示
	ns->dlg_msg = lv_label_create(ns->dlg);
	lv_label_set_text(ns->dlg_msg, "给笔记起个名字");
	lv_obj_set_style_text_font(ns->dlg_msg, &lv_font_16, 0);
	lv_obj_set_style_text_color(ns->dlg_msg, lv_color_black(), 0);
	lv_label_set_long_mode(ns->dlg_msg, LV_LABEL_LONG_CLIP);
	lv_obj_set_width(ns->dlg_msg, 220);
	lv_obj_align(ns->dlg_msg, LV_ALIGN_TOP_LEFT, 10, 4);

	ns->dlg_ta = lv_textarea_create(ns->dlg);
	lv_obj_set_size(ns->dlg_ta, 220, 28);
	lv_obj_align(ns->dlg_ta, LV_ALIGN_TOP_MID, 0, 26);
	lv_textarea_set_one_line(ns->dlg_ta, true);
	lv_textarea_set_max_length(ns->dlg_ta, NOTE_NAME_MAX);
	lv_obj_set_style_pad_all(ns->dlg_ta, 2, LV_PART_MAIN);
	if (ns->name[0] != '\0') lv_textarea_set_text(ns->dlg_ta, ns->name);// 已有名字就预填

	lv_obj_t * btn_cancel = note_create_btn(ns->dlg, "取消", 60, 26);
	lv_obj_align(btn_cancel, LV_ALIGN_TOP_LEFT, 10, 56);
	lv_obj_add_event_cb(btn_cancel, note_name_dlg_cancel_cb, LV_EVENT_CLICKED, NULL);

	lv_obj_t * btn_ok = note_create_btn(ns->dlg, "确定", 60, 26);
	lv_obj_align(btn_ok, LV_ALIGN_TOP_RIGHT, -10, 56);
	lv_obj_add_event_cb(btn_ok, note_name_dlg_ok_cb, LV_EVENT_CLICKED, NULL);

	Create_Keyboard(ns->dlg_ta);// 键盘绑到名称输入框
	lv_obj_move_to_index(ns->dlg, -1);// 提到最前, 免得被键盘点击层挡住
}

// 「还没保存」确认框: 保存 / 不保存 / 取消
static void note_open_ask_dlg(void)
{
	note_close_dlg();

	ns->dlg = note_create_dlg(30, 94);

	ns->dlg_msg = lv_label_create(ns->dlg);
	lv_label_set_text(ns->dlg_msg, "这篇笔记还没保存\n要保存吗?");
	lv_obj_set_style_text_font(ns->dlg_msg, &lv_font_16, 0);
	lv_obj_set_style_text_align(ns->dlg_msg, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(ns->dlg_msg, 216);
	lv_obj_align(ns->dlg_msg, LV_ALIGN_TOP_MID, 0, 12);

	lv_obj_t * btn_save = note_create_btn(ns->dlg, "保存", 68, 26);
	lv_obj_align(btn_save, LV_ALIGN_TOP_LEFT, 8, 58);
	lv_obj_add_event_cb(btn_save, note_ask_dlg_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)NOTE_CHOICE_SAVE);

	lv_obj_t * btn_drop = note_create_btn(ns->dlg, "不保存", 68, 26);
	lv_obj_align(btn_drop, LV_ALIGN_TOP_LEFT, 86, 58);
	lv_obj_add_event_cb(btn_drop, note_ask_dlg_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)NOTE_CHOICE_DROP);

	lv_obj_t * btn_stay = note_create_btn(ns->dlg, "取消", 68, 26);
	lv_obj_align(btn_stay, LV_ALIGN_TOP_LEFT, 164, 58);
	lv_obj_add_event_cb(btn_stay, note_ask_dlg_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)NOTE_CHOICE_STAY);

	lv_obj_move_to_index(ns->dlg, -1);// 提到最前, 别被键盘点击层挡住
}

// 通用提示弹窗: 只说明原因
static void note_open_tip_dlg(const char * msg)
{
	note_close_dlg();

	ns->ask = NOTE_ASK_NONE;
	ns->dlg = note_create_dlg(30, 86);

	ns->dlg_msg = lv_label_create(ns->dlg);
	lv_label_set_text(ns->dlg_msg, msg);
	lv_obj_set_style_text_font(ns->dlg_msg, &lv_font_16, 0);
	lv_obj_set_style_text_align(ns->dlg_msg, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(ns->dlg_msg, 216);
	lv_obj_align(ns->dlg_msg, LV_ALIGN_TOP_MID, 0, 10);

	lv_obj_t * btn_ok = note_create_btn(ns->dlg, "知道了", 72, 26);
	lv_obj_align(btn_ok, LV_ALIGN_TOP_MID, 0, 52);
	lv_obj_add_event_cb(btn_ok, note_dismiss_cb, LV_EVENT_CLICKED, NULL);

	lv_obj_move_to_index(ns->dlg, -1);// 提到最前, 别被键盘点击层挡住
}
