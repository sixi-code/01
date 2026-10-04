#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include "variables.h"
#include "defines.h"
#include "keyboard.h"
#include "list_unit.h"
#include "malloc.h"
#include "page_manager.h"
#include "ff.h"
#include "note_unit.h"

// 「还没保存」确认框是否在等答复
#define NOTE_ASK_NONE  0 // 无
#define NOTE_ASK_LEAVE 1 // 要离开本页面

// 笔记正文相关上限 (是 defines.h 里 NOTE_FILE_MAX 的组成项)
#define NOTE_MAX_CHAR  1024        // 正文最大字符数 (与文本框上限一致)
#define NOTE_MAX_BYTE  (NOTE_MAX_CHAR * 3) // 正文最大字节数 (UTF-8 下一个汉字占 3 字节)
#define NOTE_BOM_LEN   3           // UTF-8 BOM 长度

// 确认框的三个选择
enum {
	NOTE_CHOICE_SAVE = 0, // 保存
	NOTE_CHOICE_DROP,     // 不保存
	NOTE_CHOICE_STAY      // 取消
};

// 笔记名校验结果
enum {
	NOTE_NAME_OK = 0,   // 通过
	NOTE_NAME_BAD       // 空 / 含非法字符 / 过长
};

// 写文件时带上的 UTF-8 BOM
static const uint8_t NOTE_BOM[3] = {0xEF, 0xBB, 0xBF};

// 编辑状态
typedef struct {
	lv_obj_t * cont;         // 页面主容器
	lv_obj_t * ta_note;      // 正文文本框
	lv_obj_t * label_name;   // 顶栏右侧的当前笔记名
	lv_obj_t * dlg;          // 当前弹窗
	lv_obj_t * dlg_ta;       // 起名弹窗的名称输入框
	lv_obj_t * dlg_msg;      // 起名弹窗的标题兼错误提示 / 确认框的正文
	lv_style_t style_note_bg;// 正文文本框样式
	char       name[NOTE_NAME_MAX + 1]; // 当前笔记名 (不含后缀), 空串表示还没起名
	uint8_t    dirty;        // 正文是否被改过
	uint8_t    ask;          // 确认框是否在等"离开本页"的答复 NOTE_ASK_*
	uint8_t    exit_ok;      // 是否放行页面切换
	uint8_t    save_pending; // 写盘已交给后台任务, 正在等它完成
} note_state_t;

static note_state_t *ns = NULL;

// 内部函数声明
static void note_ta_event_cb(lv_event_t * e);
static void note_list_btn_event_cb(lv_event_t * e);
static void note_name_dlg_ok_cb(lv_event_t * e);
static void note_name_dlg_cancel_cb(lv_event_t * e);
static void note_ask_dlg_event_cb(lv_event_t * e);
static void note_close_dlg(void);
static void note_dlg_err(const char * msg);
static void note_update_name_label(void);
static void note_abort_switch(void);
static void note_save_ok(void);
static void note_save_err(uint8_t res);
static void note_trim(char * s);
static uint8_t note_check_name(const char * name);
static uint8_t note_fr_to_result(FRESULT res);
static void Note_Build_Path(char * out, const char * name);
int do_write_text(const char * path, const char * text);
static uint8_t note_submit_save(const char * name);
static void note_load_from_file(void);
static void note_open_name_dlg(void);
static void note_open_ask_dlg(void);

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
static void Note_Build_Path(char * out, const char * name)
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

// 写盘成功后的收尾: 放行本次页面切换
static void note_save_ok(void)
{
	ns->dirty = 0;
	ns->ask = NOTE_ASK_NONE;
	ns->exit_ok = 1;// 放行本次页面切换

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

// 把当前笔记的正文读回文本框
static void note_load_from_file(void)
{
	if (ns == NULL || ns->ta_note == NULL) return;
	if (ns->name[0] == '\0') return;// 新建的笔记没有文件可读

	char * path = malloc_bsc(NOTE_PATH_MAX);
	if (path == NULL) return;
	Note_Build_Path(path, ns->name);

	FIL file;
	FRESULT res = f_open(&file, path, FA_READ);
	free_bsc(path);// 路径用完就还, 后面只靠文件句柄读写
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
	if (total >= NOTE_BOM_LEN && (uint8_t)buf[0] == 0xEF &&
		(uint8_t)buf[1] == 0xBB && (uint8_t)buf[2] == 0xBF) {
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

	// 路径与正文各复制一份交给任务, 任务用完自己释放
	char * src = malloc_bsc(NOTE_PATH_MAX);
	char * dst = malloc_bsc(strlen(txt) + 1);
	if (src == NULL || dst == NULL) {
		if (src != NULL) free_bsc(src);
		if (dst != NULL) free_bsc(dst);
		return FILEOP_RES_FAIL;
	}
	Note_Build_Path(src, name);
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

void Create_Note_Unit(void)
{
	if (ns != NULL) return;

	ns = (note_state_t *)malloc_ccm(sizeof(note_state_t));
	if (!ns) return;
	memset(ns, 0, sizeof(note_state_t));

	// 取列表页传过来的笔记名, 空串表示新建 (缓冲区申请失败时按新建处理)
	strncpy(ns->name, page_pick_name ? page_pick_name : "", NOTE_NAME_MAX);
	ns->name[NOTE_NAME_MAX] = '\0';

	init_custom_styles();

	ns->cont = lv_obj_create(lv_scr_act());
	lv_obj_set_size(ns->cont, 240, 180);
	lv_obj_center(ns->cont);
	lv_obj_clear_flag(ns->cont, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_border_width(ns->cont, 0, LV_PART_MAIN);
	lv_obj_set_style_pad_all(ns->cont, 0, LV_PART_MAIN);
	lv_obj_set_style_bg_color(ns->cont, lv_color_hex(0xE8E8E8), LV_PART_MAIN);

	// 顶栏: 列表 / 当前笔记名
	lv_obj_t * btn_list = List_Create_Btn(ns->cont, "列表", 56, 24);
	lv_obj_align(btn_list, LV_ALIGN_TOP_LEFT, 10, 2);
	lv_obj_add_event_cb(btn_list, note_list_btn_event_cb, LV_EVENT_CLICKED, NULL);

	ns->label_name = lv_label_create(ns->cont);
	lv_label_set_text(ns->label_name, "未命名");
	lv_obj_set_style_text_font(ns->label_name, &lv_font_16, 0);
	lv_label_set_long_mode(ns->label_name, LV_LABEL_LONG_CLIP);
	lv_obj_set_width(ns->label_name, 158);
	lv_obj_set_style_text_align(ns->label_name, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_align(ns->label_name, LV_ALIGN_TOP_LEFT, 72, 7);

	// 正文
	ns->ta_note = lv_textarea_create(ns->cont);
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

	// 列表页传过来的名字用完了, 还给内存池 (与文件管理器退出时释放 current_path 同一约定)
	if (page_pick_name != NULL) {
		free_bsc(page_pick_name);
		page_pick_name = NULL;
	}

	free_ccm(ns);
	ns = NULL;
}

// 页面切换前的询问: 有未保存改动就弹确认框并拦下切换
bool Note_Unit_Can_Exit(void)
{
	if (ns == NULL) return true;
	if (!ns->dirty || ns->exit_ok) return true;

	if (ns->dlg == NULL) {// 还没弹过
		ns->ask = NOTE_ASK_LEAVE;
		note_open_ask_dlg();
	}
	// 弹窗已开着(确认框或起名框)时保持拦下, 等用户在弹窗里表态
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

// 点列表按钮: 回列表走页面管理器的历史栈, 有未保存改动时会被 Can_Exit 拦下弹确认框
static void note_list_btn_event_cb(lv_event_t * e)
{
	Page_Back();
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
	char * path = malloc_bsc(NOTE_PATH_MAX);
	if (path == NULL) {
		note_dlg_err("内存不足");
		return;
	}
	FILINFO fno;
	Note_Build_Path(path, name);
	if (f_stat(path, &fno) == FR_OK && strcmp(name, ns->name) != 0) {
		free_bsc(path);
		note_dlg_err("名称已存在");
		return;
	}
	free_bsc(path);

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

	if (ask == NOTE_ASK_LEAVE) note_abort_switch();// 这次离开是被确认框带上来的, 一并取消
}

// 「还没保存」确认框的三个按钮
static void note_ask_dlg_event_cb(lv_event_t * e)
{
	int choice = (int)(intptr_t)lv_event_get_user_data(e);

	if (ns->save_pending) return;// 写盘还在后台跑, 先不接受按键

	if (choice == NOTE_CHOICE_STAY) {
		ns->ask = NOTE_ASK_NONE;
		note_close_dlg();
		note_abort_switch();// 取消离开, 留在本页
		return;
	}

	if (choice == NOTE_CHOICE_DROP) {
		ns->ask = NOTE_ASK_NONE;
		ns->dirty = 0;// 丢弃改动
		ns->exit_ok = 1;// 放行页面切换
		note_close_dlg();
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

	ns->dlg = List_Create_Dlg(30, 86);

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

	lv_obj_t * btn_cancel = List_Create_Btn(ns->dlg, "取消", 60, 26);
	lv_obj_align(btn_cancel, LV_ALIGN_TOP_LEFT, 10, 56);
	lv_obj_add_event_cb(btn_cancel, note_name_dlg_cancel_cb, LV_EVENT_CLICKED, NULL);

	lv_obj_t * btn_ok = List_Create_Btn(ns->dlg, "确定", 60, 26);
	lv_obj_align(btn_ok, LV_ALIGN_TOP_RIGHT, -10, 56);
	lv_obj_add_event_cb(btn_ok, note_name_dlg_ok_cb, LV_EVENT_CLICKED, NULL);

	Create_Keyboard(ns->dlg_ta);// 键盘绑到名称输入框
	lv_obj_move_to_index(ns->dlg, -1);// 提到最前, 免得被键盘点击层挡住
}

// 「还没保存」确认框: 保存 / 不保存 / 取消
static void note_open_ask_dlg(void)
{
	note_close_dlg();

	ns->dlg = List_Create_Dlg(30, 94);

	ns->dlg_msg = lv_label_create(ns->dlg);
	lv_label_set_text(ns->dlg_msg, "这篇笔记还没保存\n要保存吗?");
	lv_obj_set_style_text_font(ns->dlg_msg, &lv_font_16, 0);
	lv_obj_set_style_text_align(ns->dlg_msg, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(ns->dlg_msg, 216);
	lv_obj_align(ns->dlg_msg, LV_ALIGN_TOP_MID, 0, 12);

	lv_obj_t * btn_save = List_Create_Btn(ns->dlg, "保存", 68, 26);
	lv_obj_align(btn_save, LV_ALIGN_TOP_LEFT, 8, 58);
	lv_obj_add_event_cb(btn_save, note_ask_dlg_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)NOTE_CHOICE_SAVE);

	lv_obj_t * btn_drop = List_Create_Btn(ns->dlg, "不保存", 68, 26);
	lv_obj_align(btn_drop, LV_ALIGN_TOP_LEFT, 86, 58);
	lv_obj_add_event_cb(btn_drop, note_ask_dlg_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)NOTE_CHOICE_DROP);

	lv_obj_t * btn_stay = List_Create_Btn(ns->dlg, "取消", 68, 26);
	lv_obj_align(btn_stay, LV_ALIGN_TOP_LEFT, 164, 58);
	lv_obj_add_event_cb(btn_stay, note_ask_dlg_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)NOTE_CHOICE_STAY);

	lv_obj_move_to_index(ns->dlg, -1);// 提到最前, 别被键盘点击层挡住
}
