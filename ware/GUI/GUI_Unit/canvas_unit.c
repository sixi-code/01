#include "text_unit.h"
#include "lv_port_disp.h"
#include "variables.h"
#include "ff.h"
#include "program.h"
#include "page_manager.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "variables.h"
#include "defines.h"
#include "malloc.h"
#include "keyboard.h"
#include "list_unit.h"

extern const lv_img_dsc_t canvas_before;
extern const lv_img_dsc_t canvas_big;
extern const lv_img_dsc_t canvas_exit;
extern const lv_img_dsc_t canvas_load;
extern const lv_img_dsc_t canvas_next;
extern const lv_img_dsc_t canvas_pencil;
extern const lv_img_dsc_t canvas_rubber;
extern const lv_img_dsc_t canvas_save;
extern const lv_img_dsc_t canvas_small;

static uint16_t pen_h = 0;   // 0-359 // 色调
static uint8_t  pen_s = 100; // 0-100 // 饱和度
static uint8_t  pen_v = 0;   // 0-100 (默认0即黑色)

static lv_obj_t * canvas_cont = NULL;   // 画布容器
static lv_obj_t * pixel_canvas = NULL;  // 像素画布  
static lv_color_t * canvas_buf = NULL;  // 画布缓冲区

static lv_obj_t * float_panel = NULL;   // 浮动栏

static lv_obj_t * minimap = NULL;       // 缩略图
static lv_obj_t * minimap_box = NULL;   // 缩略图黑框

#define MAX_ZOOM      12
#define MIN_ZOOM      1

static int current_zoom = 6;      // 当前缩放比例
static uint8_t current_tool = 0;  // 0: 画笔, 1: 橡皮擦(白)
static lv_color_t pen_color;      // 画笔当前颜色

static bool ignore_draw = false;  // 用于阻止关闭浮动栏时的穿透绘图

static uint8_t float_page = 0;    // 浮动栏页码，0: 工具页, 1: 颜色选择页

// 手动记录画布的位置坐标
static int canvas_pos_x = 0;      // 画布水平位置
static int canvas_pos_y = 0;      // 画布垂直位置

// --- 撤销/重做 历史记录结构 ---
#define MAX_HISTORY_STEPS 10
static lv_color_t * history_buf[MAX_HISTORY_STEPS];
static int history_idx = -1;      // 历史记录索引
static int history_max = -1;      // 历史记录最大索引

// 画作文件读写结果
enum {
	CANVAS_FILE_OK = 0, // 成功
	CANVAS_FILE_FAIL    // 打开失败 / 格式不符 / 写入出错
};

// 画作名校验结果
enum {
	CANVAS_NAME_OK = 0, // 通过
	CANVAS_NAME_BAD     // 空 / 含非法字符 / 过长
};

// 浮动栏按钮请求的延迟动作, 在 Update 里消费, 避免回调里拆建对象树
enum {
	CANVAS_ACT_NONE = 0, // 无
	CANVAS_ACT_SAVE      // 打开命名保存弹窗
};

static uint8_t pending_action = CANVAS_ACT_NONE; // 待处理的延迟动作

// 「还没保存」确认框是否在等答复
#define CANVAS_ASK_NONE  0 // 无
#define CANVAS_ASK_LEAVE 1 // 要离开本页面

// 确认框的三个选择
enum {
	CANVAS_CHOICE_SAVE = 0, // 保存
	CANVAS_CHOICE_DROP,     // 不保存
	CANVAS_CHOICE_STAY      // 取消
};

static uint8_t canvas_dirty = 0;             // 画布改过还没保存
static uint8_t canvas_ask = CANVAS_ASK_NONE; // 确认框是否在等"离开本页"的答复
static uint8_t canvas_exit_ok = 0;           // 是否放行本次页面切换

// 命名保存 / 确认弹窗状态 (按钮/弹窗/提示都由通用组件 list_unit 提供)
static lv_obj_t * dlg = NULL;      // 当前弹窗
static lv_obj_t * dlg_ta = NULL;   // 名称输入框
static lv_obj_t * dlg_msg = NULL;  // 弹窗标题兼错误提示 / 确认框正文
static char canvas_name[CANVAS_NAME_MAX + 1]; // 当前画作名(不含后缀), 空串表示还没起名

// 前置声明
static void build_float_panel_content(lv_obj_t * panel);
static uint8_t Save_Canvas_To_BMP(const char * path);
static uint8_t Load_Canvas_From_BMP(const char * path);
static void canvas_open_name_dlg(void);
static void canvas_open_ask_dlg(void);
static void canvas_close_dlg(void);
static void canvas_dlg_err(const char * msg);
static void canvas_name_dlg_ok_cb(lv_event_t * e);
static void canvas_name_dlg_cancel_cb(lv_event_t * e);
static void canvas_ask_dlg_event_cb(lv_event_t * e);
static void canvas_abort_switch(void);
static uint8_t canvas_save_named(void);
static void canvas_load_entered(void);
static uint8_t canvas_overlay_open(void);

// --- 更新略缩图与黑框显示状态 ---
static void update_minimap(void)
{
    if(!minimap || !minimap_box) return;

    int w = CANVAS_WIDTH * current_zoom;
    int h = CANVAS_HEIGHT * current_zoom;

    bool show = (w > 240 || h > 240);
    if (!show) {
        lv_obj_add_flag(minimap, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(minimap_box, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(minimap, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(minimap_box, LV_OBJ_FLAG_HIDDEN);

    // 计算缩略图上的视野框大小
    int box_w = (240 * CANVAS_WIDTH) / w; 
    int box_h = (240 * CANVAS_HEIGHT) / h;
    if(box_w > CANVAS_WIDTH) box_w = CANVAS_WIDTH;
    if(box_h > CANVAS_HEIGHT) box_h = CANVAS_HEIGHT;

    lv_obj_set_size(minimap_box, box_w, box_h);

    // 手动计算框的位置：画布向左/上偏移（负值），对应的略缩图框向右/下移动
    int map_x = 10;
    int map_y = 10;
    
    if (w > 240) {
        map_x = 10 + ((-canvas_pos_x) * CANVAS_WIDTH) / w;
    }
    if (h > 240) {
        map_y = 10 + ((-canvas_pos_y) * CANVAS_HEIGHT) / h;
    }
    
    lv_obj_set_pos(minimap_box, map_x, map_y);
}

// 缩放刷新应用
static void apply_zoom(void)
{
    lv_img_set_zoom(pixel_canvas, 256 * current_zoom);
    
    int w = CANVAS_WIDTH * current_zoom;
    int h = CANVAS_HEIGHT * current_zoom;

    // 大小判定：如果小于等于屏幕，交给系统原生居中（完美消除1pix缝隙）
    if (w <= 240 && h <= 240) {
        canvas_pos_x = (240 - w) / 2;
        canvas_pos_y = (240 - h) / 2;
        lv_obj_align(pixel_canvas, LV_ALIGN_CENTER, 0, 0);
    } else {
        // 只有当画布大于屏幕时，才使用绝对坐标，并进行边缘约束
        lv_obj_align(pixel_canvas, LV_ALIGN_TOP_LEFT, 0, 0); // 必须先重置对齐基准
        
        if (w <= 240) {
            canvas_pos_x = (240 - w) / 2;
        } else {
            if (canvas_pos_x > 0) canvas_pos_x = 0;
            if (canvas_pos_x < 240 - w) canvas_pos_x = 240 - w;
        }

        if (h <= 240) {
            canvas_pos_y = (240 - h) / 2;
        } else {
            if (canvas_pos_y > 0) canvas_pos_y = 0;
            if (canvas_pos_y < 240 - h) canvas_pos_y = 240 - h;
        }
        
        lv_obj_set_pos(pixel_canvas, canvas_pos_x, canvas_pos_y);
    }

    update_minimap();
}

// --- 保存画布相关的函数 ---
static void save_history_step(void)
{
    if (history_idx < history_max) {
        for (int i = history_idx + 1; i <= history_max; i++) {
            if (history_buf[i]) {
                free_bsc(history_buf[i]);
                history_buf[i] = NULL;
            }
        }
    }

    history_idx++;
    if (history_idx >= MAX_HISTORY_STEPS) {
        if (history_buf[0]) free_bsc(history_buf[0]);
        for (int i = 0; i < MAX_HISTORY_STEPS - 1; i++) {
            history_buf[i] = history_buf[i + 1];
        }
        history_idx = MAX_HISTORY_STEPS - 1;
    }
    history_max = history_idx;

    uint32_t buf_size = LV_CANVAS_BUF_SIZE_TRUE_COLOR(CANVAS_WIDTH, CANVAS_HEIGHT);
    history_buf[history_idx] = (lv_color_t *)malloc_bsc(buf_size);
    if (history_buf[history_idx] != NULL) {
        memcpy(history_buf[history_idx], canvas_buf, buf_size);
    }
}

static void undo_step(void)
{
    if (history_idx > 0) {
        history_idx--;
        uint32_t buf_size = LV_CANVAS_BUF_SIZE_TRUE_COLOR(CANVAS_WIDTH, CANVAS_HEIGHT);
        memcpy(canvas_buf, history_buf[history_idx], buf_size);
        lv_obj_invalidate(pixel_canvas);
        canvas_dirty = 1;// 撤回也改了内容
    }
}

static void redo_step(void)
{
    if (history_idx < history_max) {
        history_idx++;
        uint32_t buf_size = LV_CANVAS_BUF_SIZE_TRUE_COLOR(CANVAS_WIDTH, CANVAS_HEIGHT);
        memcpy(canvas_buf, history_buf[history_idx], buf_size);
        lv_obj_invalidate(pixel_canvas);
        canvas_dirty = 1;// 取消撤回也改了内容
    }
}

static void clear_history(void)
{
    for (int i = 0; i < MAX_HISTORY_STEPS; i++) {
        if (history_buf[i]) {
            free_bsc(history_buf[i]);
            history_buf[i] = NULL;
        }
    }
    history_idx = -1;
    history_max = -1;
}

// BMP文件头结构体配置(1字节对齐)
#pragma pack(push, 1)
typedef struct {
    uint16_t bfType;      // 文件类型，必须为BM
    uint32_t bfSize;      // 文件大小
    uint16_t bfReserved1; // 保留字段1，必须为0
    uint16_t bfReserved2; // 保留字段2，必须为0
    uint32_t bfOffBits;   // 像素数据偏移量
} BMP_FILE_HEADER;

typedef struct {
    uint32_t biSize;          // 信息头大小
    int32_t  biWidth;         // 图像宽度
    int32_t  biHeight;        // 图像高度
    uint16_t biPlanes;        // 面数，必须为1
    uint16_t biBitCount;      // 每个像素的位数，必须为24
    uint32_t biCompression;   // 压缩类型，必须为0
    uint32_t biSizeImage;     // 图像数据大小
    int32_t  biXPelsPerMeter; // 水平像素数/米，必须为0
    int32_t  biYPelsPerMeter; // 垂直像素数/米，必须为0
    uint32_t biClrUsed;       // 实际使用的颜色数，必须为0
    uint32_t biClrImportant;  // 重要颜色数，必须为0
} BMP_INFO_HEADER;
#pragma pack(pop)

static uint8_t Save_Canvas_To_BMP(const char * path)
{
	FIL file;
	UINT bw;

	if (canvas_buf == NULL) return CANVAS_FILE_FAIL;

	f_mkdir(CANVAS_DIR_PATH);// 创建画作目录, 已存在时忽略错误

	if (f_open(&file, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return CANVAS_FILE_FAIL;

	BMP_FILE_HEADER bfh = {0};
	BMP_INFO_HEADER bih = {0};

	int row_size = ((CANVAS_WIDTH * 3) + 3) & ~3;
	int image_size = row_size * CANVAS_HEIGHT;

	bfh.bfType = 0x4D42;
	bfh.bfSize = sizeof(BMP_FILE_HEADER) + sizeof(BMP_INFO_HEADER) + image_size;
	bfh.bfOffBits = sizeof(BMP_FILE_HEADER) + sizeof(BMP_INFO_HEADER);

	bih.biSize = sizeof(BMP_INFO_HEADER);
	bih.biWidth = CANVAS_WIDTH;
	bih.biHeight = -CANVAS_HEIGHT;
	bih.biPlanes = 1;
	bih.biBitCount = 24;

	uint8_t result = CANVAS_FILE_OK;

	if (f_write(&file, &bfh, sizeof(bfh), &bw) != FR_OK || bw != sizeof(bfh)) result = CANVAS_FILE_FAIL;

	if (result == CANVAS_FILE_OK &&
		(f_write(&file, &bih, sizeof(bih), &bw) != FR_OK || bw != sizeof(bih))) {
		result = CANVAS_FILE_FAIL;
	}

	uint8_t * row_buf = (uint8_t *)malloc_bsc(row_size);
	if (row_buf == NULL) {
		f_close(&file);
		return CANVAS_FILE_FAIL;
	}

	for (int y = 0; y < CANVAS_HEIGHT && result == CANVAS_FILE_OK; y++) {
		memset(row_buf, 0, row_size);
		for (int x = 0; x < CANVAS_WIDTH; x++) {
			lv_color_t color = canvas_buf[y * CANVAS_WIDTH + x];
			row_buf[x * 3 + 0] = (color.ch.blue << 3)  | (color.ch.blue >> 2);
			row_buf[x * 3 + 1] = (color.ch.green << 2) | (color.ch.green >> 4);
			row_buf[x * 3 + 2] = (color.ch.red << 3)   | (color.ch.red >> 2);
		}
		if (f_write(&file, row_buf, row_size, &bw) != FR_OK || bw != row_size) result = CANVAS_FILE_FAIL;
	}

	free_bsc(row_buf);

	// 关闭时的落盘失败也要报出来, 否则文件可能是残缺的
	if (f_close(&file) != FR_OK) result = CANVAS_FILE_FAIL;
	return result;
}

// 对应 Case 8：加载BMP到内存
static uint8_t Load_Canvas_From_BMP(const char * path)
{
	FIL file;
	UINT br;

	if (canvas_buf == NULL) return CANVAS_FILE_FAIL;

	if (f_open(&file, path, FA_READ) != FR_OK) return CANVAS_FILE_FAIL;

	BMP_FILE_HEADER bfh;
	BMP_INFO_HEADER bih;

	f_read(&file, &bfh, sizeof(bfh), &br);
	f_read(&file, &bih, sizeof(bih), &br);

	// 校验文件合法性 (必须等于画布尺寸且为24位色)
	if (bih.biWidth != CANVAS_WIDTH || abs(bih.biHeight) != CANVAS_HEIGHT || bih.biBitCount != 24) {
		f_close(&file);
		return CANVAS_FILE_FAIL;
	}

	f_lseek(&file, bfh.bfOffBits);

	int row_size = ((CANVAS_WIDTH * 3) + 3) & ~3;
	uint8_t * row_buf = (uint8_t *)malloc_bsc(row_size);
	if (row_buf == NULL) {
		f_close(&file);
		return CANVAS_FILE_FAIL;
	}

	bool top_down = (bih.biHeight < 0);

	for (int y = 0; y < CANVAS_HEIGHT; y++) {
		f_read(&file, row_buf, row_size, &br);
		// 如果文件标明是正高度(从下到上)，则转换y轴；如果是负高度(从上到下)，直接使用。
		int dest_y = top_down ? y : (CANVAS_HEIGHT - 1 - y);

		for (int x = 0; x < CANVAS_WIDTH; x++) {
			uint8_t b = row_buf[x * 3 + 0];
			uint8_t g = row_buf[x * 3 + 1];
			uint8_t r = row_buf[x * 3 + 2];

			lv_color_t color;
			// RGB888 -> RGB565
			color.ch.blue = b >> 3;
			color.ch.green = g >> 2;
			color.ch.red = r >> 3;

			canvas_buf[dest_y * CANVAS_WIDTH + x] = color;
		}
	}

	free_bsc(row_buf);
	f_close(&file);

	lv_obj_invalidate(pixel_canvas); // 标记需要重新绘制
	update_minimap();                // 刷新略缩图
	save_history_step();             // 压入历史记录

	return CANVAS_FILE_OK;
}

// ---------------- 画作命名保存 ----------------

// 弹窗是否盖在画布上
static uint8_t canvas_overlay_open(void)
{
	return (List_Tip_Is_Open() || dlg != NULL) ? 1 : 0;
}

// 去掉字符串首尾的空格
static void canvas_trim(char * s)
{
	size_t len = strlen(s);
	size_t i = 0;
	while (i < len && s[i] == ' ') i++;
	if (i > 0) memmove(s, s + i, len - i + 1);

	len = strlen(s);
	while (len > 0 && s[len - 1] == ' ') s[--len] = '\0';
}

// 校验画作名, 返回 CANVAS_NAME_OK 或 CANVAS_NAME_BAD
static uint8_t canvas_check_name(const char * name)
{
	if (name == NULL || name[0] == '\0') return CANVAS_NAME_BAD;
	if (strlen(name) > CANVAS_NAME_MAX) return CANVAS_NAME_BAD;

	// 文件名里不能出现的字符, 另外把控制字符也挡掉
	const char * bad = "\"*/:<>?\\|";
	for (const char * p = name; *p; p++) {
		if ((uint8_t)*p < 0x20) return CANVAS_NAME_BAD;
		if (strchr(bad, (int)(uint8_t)*p) != NULL) return CANVAS_NAME_BAD;
	}
	return CANVAS_NAME_OK;
}

// 拼出某个画作名对应的完整路径
static void canvas_build_path(char * out, const char * name)
{
	sprintf(out, "%s/%s%s", CANVAS_DIR_PATH, name, CANVAS_EXT);
}

// 关闭命名弹窗
static void canvas_close_dlg(void)
{
	if (dlg == NULL) return;

	Close_Keyboard();// 先解绑键盘
	lv_obj_del(dlg);
	dlg = NULL;
	dlg_ta = NULL;
	dlg_msg = NULL;
}

// 在命名弹窗上提示一条错误
static void canvas_dlg_err(const char * msg)
{
	if (dlg_msg == NULL) return;
	lv_obj_set_style_text_color(dlg_msg, lv_color_hex(0xD32F2F), 0);
	lv_label_set_text(dlg_msg, msg);
}

// 命名弹窗: 确认后按这个名称把画布写成文件
static void canvas_open_name_dlg(void)
{
	canvas_close_dlg();

	dlg = List_Create_Dlg(30, 86);

	// 平时显示标题, 出错时显示红色提示
	dlg_msg = lv_label_create(dlg);
	lv_label_set_text(dlg_msg, "给画作起个名字");
	lv_obj_set_style_text_font(dlg_msg, &lv_font_16, 0);
	lv_obj_set_style_text_color(dlg_msg, lv_color_black(), 0);
	lv_label_set_long_mode(dlg_msg, LV_LABEL_LONG_CLIP);
	lv_obj_set_width(dlg_msg, 220);
	lv_obj_align(dlg_msg, LV_ALIGN_TOP_LEFT, 10, 4);

	dlg_ta = lv_textarea_create(dlg);
	lv_obj_set_size(dlg_ta, 220, 28);
	lv_obj_align(dlg_ta, LV_ALIGN_TOP_MID, 0, 26);
	lv_textarea_set_one_line(dlg_ta, true);
	lv_textarea_set_max_length(dlg_ta, CANVAS_NAME_MAX);
	lv_obj_set_style_pad_all(dlg_ta, 2, LV_PART_MAIN);
	if (canvas_name[0] != '\0') lv_textarea_set_text(dlg_ta, canvas_name);// 已有名字就预填

	lv_obj_t * btn_cancel = List_Create_Btn(dlg, "取消", 60, 26);
	lv_obj_align(btn_cancel, LV_ALIGN_TOP_LEFT, 10, 56);
	lv_obj_add_event_cb(btn_cancel, canvas_name_dlg_cancel_cb, LV_EVENT_CLICKED, NULL);

	lv_obj_t * btn_ok = List_Create_Btn(dlg, "确定", 60, 26);
	lv_obj_align(btn_ok, LV_ALIGN_TOP_RIGHT, -10, 56);
	lv_obj_add_event_cb(btn_ok, canvas_name_dlg_ok_cb, LV_EVENT_CLICKED, NULL);

	Create_Keyboard(dlg_ta);// 键盘绑到名称输入框
	lv_obj_move_to_index(dlg, -1);// 提到最前, 免得被键盘点击层挡住
}

// 命名弹窗点确定: 校验名称 + 查重, 通过就写盘
static void canvas_name_dlg_ok_cb(lv_event_t * e)
{
	char name[CANVAS_NAME_MAX + 1];
	const char * src;

	if (dlg_ta == NULL) return;

	// 名称按字节数限长, 超了直接报错
	src = lv_textarea_get_text(dlg_ta);
	if (src == NULL || strlen(src) > CANVAS_NAME_MAX) {
		canvas_dlg_err("名称太长");
		return;
	}
	strcpy(name, src);
	canvas_trim(name);

	if (canvas_check_name(name) != CANVAS_NAME_OK) {
		canvas_dlg_err((name[0] == '\0') ? "请先输入名称" : "名称含非法字符");
		return;
	}

	// 查重: 同名且不是当前这张就报错
	char * path = malloc_bsc(CANVAS_PATH_MAX);
	if (path == NULL) {
		canvas_dlg_err("内存不足");
		return;
	}
	FILINFO fno;
	canvas_build_path(path, name);
	if (f_stat(path, &fno) == FR_OK && strcmp(name, canvas_name) != 0) {
		free_bsc(path);
		canvas_dlg_err("名称已存在");
		return;
	}

	uint8_t res = Save_Canvas_To_BMP(path);
	free_bsc(path);

	if (res != CANVAS_FILE_OK) {
		canvas_dlg_err("写入失败\n请检查 SD 卡");
		return;
	}

	strncpy(canvas_name, name, CANVAS_NAME_MAX);
	canvas_name[CANVAS_NAME_MAX] = '\0';
	canvas_dirty = 0;

	if (canvas_ask == CANVAS_ASK_LEAVE) {
		// 这次起名是"离开前保存"带上来的: 存完直接放行切换, 不用再弹"已保存"
		canvas_ask = CANVAS_ASK_NONE;
		canvas_exit_ok = 1;
		canvas_close_dlg();
		return;
	}

	canvas_close_dlg();
	List_Open_Tip_Dlg("已保存");
}

// 命名弹窗点取消: 放弃保存
static void canvas_name_dlg_cancel_cb(lv_event_t * e)
{
	uint8_t ask = canvas_ask;
	canvas_ask = CANVAS_ASK_NONE;
	canvas_close_dlg();

	if (ask == CANVAS_ASK_LEAVE) canvas_abort_switch();// 这次离开是被确认框带上来的, 一并取消
}

// 取消本次离开: 把切换目标改回当前页
static void canvas_abort_switch(void)
{
	Page_Request_Switch(Page_Get_Current());
}

// 把当前画布按已有的画作名写回 SD, 返回 CANVAS_FILE_*
static uint8_t canvas_save_named(void)
{
	char * path = malloc_bsc(CANVAS_PATH_MAX);
	uint8_t res;

	if (path == NULL) return CANVAS_FILE_FAIL;

	canvas_build_path(path, canvas_name);
	res = Save_Canvas_To_BMP(path);
	free_bsc(path);
	return res;
}

// 「还没保存」确认框: 保存 / 不保存 / 取消
static void canvas_open_ask_dlg(void)
{
	canvas_close_dlg();

	// 浮动栏还开着就收掉, 免得它在弹窗下面露着
	if (float_panel != NULL) {
		lv_obj_del(float_panel);
		float_panel = NULL;
		float_page = 0;
	}

	dlg = List_Create_Dlg(30, 94);

	dlg_msg = lv_label_create(dlg);
	lv_label_set_text(dlg_msg, "这幅画还没保存\n要保存吗?");
	lv_obj_set_style_text_font(dlg_msg, &lv_font_16, 0);
	lv_obj_set_style_text_align(dlg_msg, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(dlg_msg, 216);
	lv_obj_align(dlg_msg, LV_ALIGN_TOP_MID, 0, 12);

	lv_obj_t * btn_save = List_Create_Btn(dlg, "保存", 68, 26);
	lv_obj_align(btn_save, LV_ALIGN_TOP_LEFT, 8, 58);
	lv_obj_add_event_cb(btn_save, canvas_ask_dlg_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)CANVAS_CHOICE_SAVE);

	lv_obj_t * btn_drop = List_Create_Btn(dlg, "不保存", 68, 26);
	lv_obj_align(btn_drop, LV_ALIGN_TOP_LEFT, 86, 58);
	lv_obj_add_event_cb(btn_drop, canvas_ask_dlg_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)CANVAS_CHOICE_DROP);

	lv_obj_t * btn_stay = List_Create_Btn(dlg, "取消", 68, 26);
	lv_obj_align(btn_stay, LV_ALIGN_TOP_LEFT, 164, 58);
	lv_obj_add_event_cb(btn_stay, canvas_ask_dlg_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)CANVAS_CHOICE_STAY);

	lv_obj_move_to_index(dlg, -1);// 提到最前, 别被键盘点击层挡住
}

// 「还没保存」确认框的三个按钮
static void canvas_ask_dlg_event_cb(lv_event_t * e)
{
	int choice = (int)(intptr_t)lv_event_get_user_data(e);

	if (choice == CANVAS_CHOICE_STAY) {
		canvas_ask = CANVAS_ASK_NONE;
		canvas_close_dlg();
		canvas_abort_switch();// 取消离开, 留在本页
		return;
	}

	if (choice == CANVAS_CHOICE_DROP) {
		canvas_ask = CANVAS_ASK_NONE;
		canvas_dirty = 0;// 丢弃改动
		canvas_exit_ok = 1;// 放行页面切换
		canvas_close_dlg();
		return;
	}

	// 选保存: 已有名字就直接覆盖, 没名字先去起名(起名完成后会放行切换)
	if (canvas_name[0] != '\0') {
		if (canvas_save_named() != CANVAS_FILE_OK) {
			canvas_dlg_err("写入失败\n请检查 SD 卡");
			return;
		}
		canvas_ask = CANVAS_ASK_NONE;
		canvas_dirty = 0;
		canvas_exit_ok = 1;
		canvas_close_dlg();
		return;
	}
	canvas_open_name_dlg();
}

// 页面切换前的询问: 有未保存改动就弹确认框并拦下切换
bool Canvas_Unit_Can_Exit(void)
{
	if (!canvas_dirty || canvas_exit_ok) return true;

	if (dlg == NULL) {// 还没弹过
		canvas_ask = CANVAS_ASK_LEAVE;
		canvas_open_ask_dlg();
	}
	// 弹窗已开着(确认框或起名框)时保持拦下, 等用户在弹窗里表态
	return false;
}

// 进入本页时若带了画作名, 就把那一张读进来当起点
static void canvas_load_entered(void)
{
	char * path;
	uint8_t res;

	if (canvas_name[0] == '\0') return;// 新建的空白画布, 没有文件要读

	path = malloc_bsc(CANVAS_PATH_MAX);
	if (path == NULL) {
		canvas_name[0] = '\0';
		return;
	}
	canvas_build_path(path, canvas_name);
	res = Load_Canvas_From_BMP(path);
	free_bsc(path);

	if (res != CANVAS_FILE_OK) {
		// 读不进来就当新画布, 免得之后一保存把原来那张覆盖掉
		canvas_name[0] = '\0';
		List_Open_Tip_Dlg("无法打开\n需要 40x40 的 24 位 BMP");
		return;
	}

	// 载入后的这一张就是起点, 不让撤回一路退成白板
	clear_history();
}

// --- 色环取色回调 ---
static void colorwheel_cb(lv_event_t * e)
{
    lv_obj_t * cw = lv_event_get_target(e);
    
    // 只从色环获取Hue (色相)
    pen_h = lv_colorwheel_get_hsv(cw).h;
    
    // 结合当前的 S 和 V 计算真实颜色
    pen_color = lv_color_hsv_to_rgb(pen_h, pen_s, pen_v);
    current_tool = 0; 

    // 更新页面切换按钮（即按键4）的背景色
    lv_obj_t * panel = lv_obj_get_parent(cw);
    lv_obj_t * btn4 = lv_obj_get_child(panel, 1); 
    if (btn4) {
        lv_obj_set_style_bg_color(btn4, pen_color, 0);
    }
}

// --- 浮动工具栏按钮回调 ---
static void float_btn_event_cb(lv_event_t * e)
{
    uint32_t btn_id = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    
    switch(btn_id) {
        case 0: undo_step(); break; // 撤回
        case 1: Page_Back(); break; // 退出
        case 2: redo_step(); break; // 取消撤回
        case 3: if(current_zoom > MIN_ZOOM) {current_zoom--;apply_zoom();} break;// 缩小
        case 4: 
            float_page = !float_page; // 翻页
            if(float_panel) build_float_panel_content(float_panel);
            break;
        case 5: if(current_zoom < MAX_ZOOM) {current_zoom++;apply_zoom();} break;// 放大
        case 6: pending_action = CANVAS_ACT_SAVE; break;// 保存为画作
        
        case 7: // 画笔/橡皮切换
        {
            current_tool = !current_tool; 
            // 立即获取触发事件的按钮，并更新其子对象（图标）的图片源
            lv_obj_t * btn = lv_event_get_target(e);
            lv_obj_t * img = lv_obj_get_child(btn, 0); // 图标是我们为该按钮创建的第一个(唯一)子对象
            if (img != NULL) {
                lv_img_set_src(img, current_tool == 0 ? &canvas_pencil : &canvas_rubber);
            }
            break;
        }
        
        case 8: Page_Back(); break;// 回画作列表换一张
    }
}

// --- HSV 调整按钮回调 ---
static void hsv_adjust_event_cb(lv_event_t * e)
{
    uint32_t action = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    int step = 10; // S和V 每次增减 10%
    int h_step = 5; // H 每次增减 5度
    
    switch (action) {
        case 0: // S-
            if (pen_s >= step) pen_s -= step; else pen_s = 0;
            break;
        case 1: // S+
            if (pen_s <= 100 - step) pen_s += step; else pen_s = 100;
            break;
        case 2: // V-
            if (pen_v >= step) pen_v -= step; else pen_v = 0;
            break;
        case 3: // V+
            if (pen_v <= 100 - step) pen_v += step; else pen_v = 100;
            break;
        case 4: // H-
            if (pen_h >= h_step) pen_h -= h_step; else pen_h = 360 - h_step + pen_h;
            break;
        case 5: // H+
            pen_h = (pen_h + h_step) % 360;
            break;
    }
    
    // 重新计算真实的画笔颜色
    pen_color = lv_color_hsv_to_rgb(pen_h, pen_s, pen_v);
    current_tool = 0;

    // 刷新中间的中心按钮颜色
    lv_obj_t * btn = lv_event_get_target(e);
    lv_obj_t * panel = lv_obj_get_parent(btn);
    lv_obj_t * btn4 = lv_obj_get_child(panel, 1); 
    if (btn4) {
        lv_obj_set_style_bg_color(btn4, pen_color, 0);
    }
}

// 辅助创建微调按钮的函数
static void create_adjust_btn(lv_obj_t * parent, int x, int y, const char * text, uint32_t action)
{
    lv_obj_t * btn = lv_obj_create(parent);
    lv_obj_set_size(btn, 22, 22);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_radius(btn, 11, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x555555), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_80, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x888888), 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t * label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_transform_zoom(label, 150, 0); // 缩小字体防止越界
    
    lv_obj_add_event_cb(btn, hsv_adjust_event_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)action);
}

// --- 动态构建浮动栏内容 ---
static void build_float_panel_content(lv_obj_t * panel)
{
    // 清除面板所有子对象
    lv_obj_clean(panel);

    if (float_page == 0) {
        // 定义第一页各个按钮的图标指针映射
        const lv_img_dsc_t * icons[9] = {
            &canvas_before,     // 0: 撤回
            &canvas_exit,       // 1: 退出
            &canvas_next,       // 2: 取消撤回
            &canvas_small,      // 3: 缩小
            NULL,               // 4: 翻页/当前颜色提示 (不用图标)
            &canvas_big,        // 5: 放大
            &canvas_save,       // 6: 保存
            current_tool == 0 ? &canvas_pencil : &canvas_rubber, // 7: 切换画笔/橡皮
            &canvas_load        // 8: 加载
        };

        // 第一页：九宫格工具
        for (int i = 0; i < 9; i++) {
            lv_obj_t * btn = lv_obj_create(panel);
            
            lv_obj_set_size(btn, 30, 30);
            
            int row = i / 3;
            int col = i % 3;
            
            // 边距10，按钮30，间距10。步进即为40
            lv_obj_set_pos(btn, 10 + col * 40, 10 + row * 40);
            
            lv_obj_set_style_radius(btn, 3, 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0xCCCCCC), 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_50, 0); 
            lv_obj_set_style_border_width(btn, 0, 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_set_style_pad_all(btn, 0, 0);
            lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
            
            // 额外给中间的按键4上个色提示当前画笔颜色
            if (i == 4) {
                lv_obj_set_style_bg_color(btn, pen_color, 0);
                lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
                lv_obj_set_style_border_width(btn, 1, 0);
                lv_obj_set_style_border_color(btn, lv_color_hex(0x000000), 0);
            }

            // 如果该位置有图标映射，则创建图片并居中
            if (icons[i] != NULL) {
                lv_obj_t * img = lv_img_create(btn);
                lv_img_set_src(img, icons[i]);
                lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
                
                // alpha_1bit 图片通常需要重新着色来确保显示（这里设为深灰色/黑色）
                lv_obj_set_style_img_recolor(img, lv_color_hex(0x333333), 0);
                lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
            }

            lv_obj_add_event_cb(btn, float_btn_event_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
        }
    } else {
        // 第二页：画笔颜色选择
        lv_obj_t * cw = lv_colorwheel_create(panel, true);
        lv_obj_set_size(cw, 120, 120); // 扩大到120
        lv_obj_align(cw, LV_ALIGN_CENTER, 0, 0);

        // 彻底隐藏滚轮（设置透明度为0）
        lv_obj_set_style_opa(cw, LV_OPA_TRANSP, LV_PART_KNOB); 
        lv_obj_set_style_arc_width(cw, 12, LV_PART_MAIN);

        // 永远将色环的 S 和 V 设为 100，保证色环始终是彩虹色！
        lv_color_hsv_t display_hsv = {pen_h, 100, 100};
        lv_colorwheel_set_hsv(cw, display_hsv); 
        
        lv_obj_add_event_cb(cw, colorwheel_cb, LV_EVENT_VALUE_CHANGED, NULL);

        // 保留原按钮4，用于切换回第一页。放在中心(130/2=65, 65-15=50)
        lv_obj_t * btn = lv_obj_create(panel);
        lv_obj_set_size(btn, 30, 30);
        lv_obj_set_pos(btn, 50, 50); // 适配130x130的绝对中心位置
        lv_obj_set_style_radius(btn, 15, 0);
        lv_obj_set_style_bg_color(btn, pen_color, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0); 
        lv_obj_set_style_border_width(btn, 2, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(btn, float_btn_event_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)4);

        // 添加六个环绕分布的调节按钮
        const char* texts[6] = {"-S", "+S", "-V", "+V", "-H", "+H"};
        uint32_t actions[6] = {0, 1, 2, 3, 4, 5};
        
        int center_x = 65;
        int center_y = 65;
        int radius = 33; // 放在中心按钮外侧，色环内侧 (15 < 33 < 48)

        for(int i = 0; i < 6; i++) {
            // 每隔60度放一个按钮 (i * PI / 3)
            double angle = i * 60.0 * 3.14159265 / 180.0;
            // 坐标减去 11 (22/2) 使得按钮中心与计算坐标对齐
            int bx = center_x + (int)(radius * cos(angle)) - 11;
            int by = center_y + (int)(radius * sin(angle)) - 11;
            
            create_adjust_btn(panel, bx, by, texts[i], actions[i]);
        }
    }
}

// --- 主画布事件处理 ---
static void canvas_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    
    // 列表浮层/弹窗盖在画布上时, 画布不接收绘制
    if (canvas_overlay_open()) return;

    // 果手指松开，重置屏蔽绘图标志位
    if (code == LV_EVENT_RELEASED) {
        ignore_draw = false;
    }

    // 检查是否点在浮动栏外并需要关闭浮动栏
    if (code == LV_EVENT_PRESSED && float_panel != NULL) {
        lv_indev_t * indev = lv_indev_get_act();
        lv_point_t point;
        lv_indev_get_point(indev, &point); 
        
        if(point.x < float_panel->coords.x1 || point.x > float_panel->coords.x2 ||
           point.y < float_panel->coords.y1 || point.y > float_panel->coords.y2) {
            lv_obj_del(float_panel);
            float_panel = NULL;
            float_page = 0; // 关闭时重置为第一页
            // 屏蔽本次按压期间的所有绘图触发
            ignore_draw = true; 
            return; 
        }
    }

    if (ignore_draw) return;

    if (code == LV_EVENT_PRESSED) {
        save_history_step();
    }

    if (code == LV_EVENT_PRESSING || code == LV_EVENT_PRESSED) {
        lv_indev_t * indev = lv_indev_get_act();
        if(indev == NULL) return;

        lv_point_t point;
        lv_indev_get_point(indev, &point); 

        // 使用画布实际在屏幕的物理位置进行换算
        lv_coord_t px_x = pixel_canvas->coords.x1;
        lv_coord_t px_y = pixel_canvas->coords.y1;

        int16_t logical_x = (int16_t)floor((point.x - px_x) / current_zoom);
        int16_t logical_y = (int16_t)floor((point.y - px_y) / current_zoom);

        if (logical_x >= 0 && logical_x < CANVAS_WIDTH && logical_y >= 0 && logical_y < CANVAS_HEIGHT) {
            // 【修改】使用笔的颜色作为绘制颜色
            lv_color_t color = (current_tool == 0) ? pen_color : lv_color_white();
            lv_canvas_set_px_color(pixel_canvas, logical_x, logical_y, color);
            lv_obj_invalidate(pixel_canvas);
            lv_obj_invalidate(minimap); // 刷新略缩图
            canvas_dirty = 1;           // 画布内容变了, 离开时要问一句
        }
    }
}

void Create_Canvas_Unit(void)
{
	pen_h = 0;
    pen_s = 100;
    pen_v = 0;
    pen_color = lv_color_black();
	
    clear_history();
    current_zoom = 6;
    ignore_draw = false;
    float_page = 0;
    pending_action = CANVAS_ACT_NONE;
    canvas_dirty = 0;
    canvas_ask = CANVAS_ASK_NONE;
    canvas_exit_ok = 0;

    // 取列表页传过来的画作名, 空串表示新建 (缓冲区申请失败时按新建处理)
    strncpy(canvas_name, page_pick_name ? page_pick_name : "", CANVAS_NAME_MAX);
    canvas_name[CANVAS_NAME_MAX] = '\0';

    pen_color = lv_color_black(); // 默认画笔颜色为黑

    // 清掉当前屏幕的滚动属性，防止底层输入设备左摇杆误触发拖拽画线
    lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);

    canvas_buf = (lv_color_t *)malloc_bsc(LV_CANVAS_BUF_SIZE_TRUE_COLOR(CANVAS_WIDTH, CANVAS_HEIGHT));
    if(canvas_buf == NULL) return;

    canvas_cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(canvas_cont, 240, 240);
    lv_obj_center(canvas_cont);
    lv_obj_set_style_bg_opa(canvas_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(canvas_cont, lv_color_hex(0x808080), 0);
    lv_obj_set_style_border_width(canvas_cont, 0, 0);
    lv_obj_set_style_radius(canvas_cont, 0, 0);
    lv_obj_set_style_pad_all(canvas_cont, 0, 0);
    
    // 确保画布容器没有滚动属性
    lv_obj_clear_flag(canvas_cont, LV_OBJ_FLAG_SCROLLABLE);

    pixel_canvas = lv_canvas_create(canvas_cont);
    lv_canvas_set_buffer(pixel_canvas, canvas_buf, CANVAS_WIDTH, CANVAS_HEIGHT, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(pixel_canvas, lv_color_white(), LV_OPA_COVER);
    lv_img_set_antialias(pixel_canvas, false);
    
    lv_img_set_size_mode(pixel_canvas, LV_IMG_SIZE_MODE_REAL);
    
    lv_obj_clear_flag(pixel_canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pixel_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(pixel_canvas, canvas_event_cb, LV_EVENT_ALL, NULL);

    // 创建略缩图
    minimap = lv_img_create(lv_scr_act());
    lv_img_set_src(minimap, lv_canvas_get_img(pixel_canvas));
    lv_obj_set_pos(minimap, 10, 10);
    lv_obj_set_size(minimap, CANVAS_WIDTH, CANVAS_HEIGHT);
    lv_obj_set_style_outline_width(minimap, 2, 0);
    lv_obj_set_style_outline_color(minimap, lv_color_hex(0x555555), 0);
    lv_obj_set_style_outline_pad(minimap, 0, 0);
    lv_obj_set_style_shadow_width(minimap, 10, 0);
    lv_obj_set_style_shadow_color(minimap, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(minimap, LV_OPA_50, 0);
    lv_obj_set_style_shadow_ofs_x(minimap, 2, 0);
    lv_obj_set_style_shadow_ofs_y(minimap, 2, 0);
    lv_obj_move_foreground(minimap);

    // 视野黑框
    minimap_box = lv_obj_create(lv_scr_act());
    lv_obj_set_style_bg_opa(minimap_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(minimap_box, lv_color_black(), 0);
    lv_obj_set_style_border_width(minimap_box, 1, 0);
    lv_obj_set_style_radius(minimap_box, 0, 0);
    lv_obj_clear_flag(minimap_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(minimap_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(minimap_box);
    
    // 初始化画布位置
    apply_zoom();

    canvas_load_entered(); // 带着画作名进来的就把那一张读进来当起点

    save_history_step();
}

void Update_Canvas_Unit(void)
{
    // 浮动栏按钮请求的动作在这里执行, 避免在回调里拆建对象树
    if (pending_action != CANVAS_ACT_NONE) {
        pending_action = CANVAS_ACT_NONE;

        if (float_panel != NULL) {
            lv_obj_del(float_panel);
            float_panel = NULL;
            float_page = 0;
        }
        canvas_open_name_dlg();
    }

    // ============================================
    // 【新增】画布边缘平移判定逻辑
    // ============================================
    lv_point_t point = {120, 120}; 
    lv_indev_t * indev = lv_indev_get_next(NULL);
    while(indev) {
        if(lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            lv_indev_get_point(indev, &point);
            break;
        }
        indev = lv_indev_get_next(indev);
    }

    if (pixel_canvas != NULL && float_panel == NULL && !canvas_overlay_open()) {
        int w = CANVAS_WIDTH * current_zoom;
        int h = CANVAS_HEIGHT * current_zoom;
        int pan_speed = 6; // 平移速度，可以根据需要调整
        bool need_update = false;

        if (w > 240) {
            if (point.x < 10) { canvas_pos_x += pan_speed; need_update = true; }
            if (point.x > 230) { canvas_pos_x -= pan_speed; need_update = true; }
            // 约束边界
            if (canvas_pos_x > 0) canvas_pos_x = 0;
            if (canvas_pos_x < 240 - w) canvas_pos_x = 240 - w;
        }

        if (h > 240) {
            if (point.y < 10) { canvas_pos_y += pan_speed; need_update = true; }
            if (point.y > 230) { canvas_pos_y -= pan_speed; need_update = true; }
            // 约束边界
            if (canvas_pos_y > 0) canvas_pos_y = 0;
            if (canvas_pos_y < 240 - h) canvas_pos_y = 240 - h;
        }

        if (need_update) {
            lv_obj_set_pos(pixel_canvas, canvas_pos_x, canvas_pos_y);
            update_minimap(); // 更新略缩图上的小黑框
        }
    }
    // ============================================

    static uint8_t last_R = 0;
    
    // R键松开检测 (呼出/隐藏浮动栏)
    if(!g_key_R_M_RT && last_R && !canvas_overlay_open())
    {
        if (float_panel == NULL) {
            float_page = 0; // 呼出时保证默认在第一页
            // 创建 130x130 浮动栏
            float_panel = lv_obj_create(lv_scr_act());
            lv_obj_set_size(float_panel, 130, 130);
            
            // 计算边界限制 (130/2 = 65)
            lv_coord_t px = point.x - 65;
            lv_coord_t py = point.y - 65;
            if (px < 0) px = 0;
            if (py < 0) py = 0;
            if (px > 240 - 130) px = 240 - 130;
            if (py > 240 - 130) py = 240 - 130;
            
            lv_obj_set_pos(float_panel, px, py);
            
            lv_obj_set_style_radius(float_panel, 11, 0);
            lv_obj_set_style_bg_color(float_panel, lv_color_white(), 0);
            // 【修改】改为纯白不透明
            lv_obj_set_style_bg_opa(float_panel, LV_OPA_COVER, 0); 
            lv_obj_set_style_border_width(float_panel, 1, 0);
            lv_obj_set_style_border_color(float_panel, lv_color_hex(0x808080), 0);
            lv_obj_set_style_shadow_width(float_panel, 0, 0);
            lv_obj_set_style_pad_all(float_panel, 0, 0);
            lv_obj_clear_flag(float_panel, LV_OBJ_FLAG_SCROLLABLE);

            // 调用子函数构建浮动面板内容
            build_float_panel_content(float_panel);
        } else {
            lv_obj_del(float_panel);
            float_panel = NULL;
            float_page = 0;
        }
    }
    
    last_R = g_key_R_M_RT;

    Update_Keyboard(); // 刷新实体键盘输入 (给画作起名时用来打字)
}

void Remove_Canvas_Unit(void)
{
    pending_action = CANVAS_ACT_NONE;

    List_Close_Tip_Dlg();// 提示弹窗不在主容器里, 手动删
    canvas_close_dlg();
    Close_Keyboard(); // 解绑键盘

    if (float_panel) {
        lv_obj_del(float_panel);
        float_panel = NULL;
    }

    if (minimap_box) {
        lv_obj_del(minimap_box);
        minimap_box = NULL;
    }

    if (minimap) {
        lv_obj_del(minimap);
        minimap = NULL;
    }

    if (canvas_cont) {
        lv_obj_del(canvas_cont);
        canvas_cont = NULL;
        pixel_canvas = NULL; 
    }

    if (canvas_buf) {
        free_bsc(canvas_buf);
        canvas_buf = NULL;
    }

    clear_history();

    // 列表页传过来的名字用完了, 还给内存池 (与文件管理器退出时释放 current_path 同一约定)
    if (page_pick_name != NULL) {
        free_bsc(page_pick_name);
        page_pick_name = NULL;
    }

    canvas_dirty = 0;
    canvas_ask = CANVAS_ASK_NONE;
    canvas_exit_ok = 0;
}
