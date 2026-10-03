#include "stm32f4xx.h"
#include "lv_port_disp.h"
#include "adc.h"
#include "key.h"
#include "variables.h"

// ================= 摇杆显示组件 =================
// 单个摇杆的全部 UI 对象（左右摇杆共用同一套结构）
typedef struct {
    lv_obj_t *panel;  // 总面板
    lv_obj_t *cont;   // 圆环区域
    lv_obj_t *cursor; // 位置光标/圆点
    lv_obj_t *label;  // 坐标文本标签
    lv_obj_t *line_h; // 水平线
    lv_obj_t *line_v; // 垂直线
} stick_ui_t;

static stick_ui_t stick_L; // 左摇杆
static stick_ui_t stick_R; // 右摇杆

// 圆环内十字辅助线端点（两个摇杆的几何尺寸相同，共用一组坐标）
static const lv_point_t stick_line_h_points[] = {{0, 43}, {90, 43}};
static const lv_point_t stick_line_v_points[] = {{43, 0}, {43, 90}};

static void stick_ui_create(stick_ui_t *s, lv_coord_t x, const char *title);
static void stick_ui_update(stick_ui_t *s, int16_t x, int16_t y, int8_t x_offset, int8_t y_offset, uint8_t pressed);
static void stick_ui_remove(stick_ui_t *s);

// 创建一个摇杆的完整 UI：总面板 + 标题 + 圆环 + 十字线 + 光标 + 坐标标签
// x: 总面板在屏幕上的横坐标（纵坐标固定 30）
// title: 顶部标题文本
static void stick_ui_create(stick_ui_t *s, lv_coord_t x, const char *title)
{
    // 总面板容器 (100x130)
    s->panel = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s->panel, 100, 130);
    lv_obj_set_pos(s->panel, x, 30);
    lv_obj_set_style_pad_all(s->panel, 0, 0);
    lv_obj_set_style_radius(s->panel, 0, 0);
    lv_obj_set_style_border_width(s->panel, 0, 0);
    lv_obj_set_style_bg_opa(s->panel, LV_OPA_0, 0); // 透明背景
    lv_obj_clear_flag(s->panel, LV_OBJ_FLAG_SCROLLABLE);

    // 顶部标题区域 (100x15)
    lv_obj_t *header = lv_obj_create(s->panel);
    lv_obj_set_size(header, 100, 15);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(header, LV_OPA_0, 0);

    lv_obj_t *header_lbl = lv_label_create(header);
    lv_label_set_text(header_lbl, title);
    lv_obj_center(header_lbl);

    // 摇杆圆环区域 (90x90, 相对面板(5,20))
    s->cont = lv_obj_create(s->panel);
    lv_obj_set_size(s->cont, 90, 90);
    lv_obj_set_pos(s->cont, 5, 20);
    lv_obj_set_style_radius(s->cont, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s->cont, LV_OPA_0, 0);
    lv_obj_set_style_border_width(s->cont, 2, 0);
    lv_obj_set_style_border_color(s->cont, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_clear_flag(s->cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s->cont, 0, 0);

    // 水平辅助线 - 相对圆环坐标 (0,43) -> (90,43)
    s->line_h = lv_line_create(s->cont);
    lv_line_set_points(s->line_h, stick_line_h_points, 2);
    lv_obj_set_style_line_width(s->line_h, 1, 0);
    lv_obj_set_style_line_color(s->line_h, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_line_opa(s->line_h, LV_OPA_50, 0);

    // 垂直辅助线 - 相对圆环坐标 (43,0) -> (43,90)
    s->line_v = lv_line_create(s->cont);
    lv_line_set_points(s->line_v, stick_line_v_points, 2);
    lv_obj_set_style_line_width(s->line_v, 1, 0);
    lv_obj_set_style_line_color(s->line_v, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_line_opa(s->line_v, LV_OPA_50, 0);

    // 光标（12x12 圆点，默认灰色）
    s->cursor = lv_obj_create(s->cont);
    lv_obj_set_size(s->cursor, 12, 12);
    lv_obj_set_style_radius(s->cursor, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s->cursor, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_center(s->cursor);
    lv_obj_set_style_border_width(s->cursor, 0, 0);
    lv_obj_set_style_shadow_width(s->cursor, 0, 0);

    // 底部数据显示区域 (100x15)
    lv_obj_t *footer = lv_obj_create(s->panel);
    lv_obj_set_size(footer, 100, 15);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(footer, 0, 0);
    lv_obj_set_style_border_width(footer, 0, 0);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(footer, LV_OPA_0, 0);

    s->label = lv_label_create(footer);
    lv_label_set_text(s->label, "X:+000 Y:+000");
    lv_obj_center(s->label);
}

// 刷新一个摇杆的 UI：光标位置、按下变色、坐标文本
// x,y: 原始轴值（用于文本显示）
// x_offset,y_offset: 已按比例换算的光标偏移量
// pressed: 摇杆是否被按下（按下淡蓝，未按下淡灰）
static void stick_ui_update(stick_ui_t *s, int16_t x, int16_t y, int8_t x_offset, int8_t y_offset, uint8_t pressed)
{
    if(s->cont == NULL || s->cursor == NULL) return;

    lv_obj_align(s->cursor, LV_ALIGN_CENTER, x_offset, y_offset);

    lv_obj_set_style_bg_color(s->cursor,
        lv_palette_main(pressed ? LV_PALETTE_BLUE : LV_PALETTE_GREY), 0);

    lv_label_set_text_fmt(s->label, "X:%+04d Y:%+04d", x, y);
}

// 删除一个摇杆的全部 UI 对象并清空指针
static void stick_ui_remove(stick_ui_t *s)
{
    if(s->panel) {
        lv_obj_del(s->panel); // 圆环/辅助线/光标/标签均为面板子对象，随面板一并删除
        s->panel = NULL;
        s->cont = NULL;
        s->cursor = NULL;
        s->line_h = NULL;
        s->line_v = NULL;
        s->label = NULL;
    }
}

void Create_Stick_Label_L(void)
{
    stick_ui_create(&stick_L, 15, "[---左摇杆---]");
}

void Update_Stick_Label_L(void)
{
    // 调整比例因子以适应90像素圆环（左摇杆 Y 轴方向取反）
    int8_t x_offset = g_key_L_X / 4;
    int8_t y_offset = -g_key_L_Y / 4;
    stick_ui_update(&stick_L, g_key_L_X, g_key_L_Y, x_offset, y_offset, g_key_L_M_RT);
}

void Remove_Stick_Label_L(void)
{
    stick_ui_remove(&stick_L);
}

void Create_Stick_Label_R(void)
{
    stick_ui_create(&stick_R, 125, "[---右摇杆---]");
}

void Update_Stick_Label_R(void)
{
    // 调整比例因子以适应90像素圆环（右摇杆 Y 轴方向不取反，比例不同）
    int8_t x_offset = g_key_R_X / 3;
    int8_t y_offset = g_key_R_Y / 3;
    stick_ui_update(&stick_R, g_key_R_X, g_key_R_Y, x_offset, y_offset, g_key_R_M_RT);
}

void Remove_Stick_Label_R(void)
{
    stick_ui_remove(&stick_R);
}

// ================= 按键显示组件 =================
// 单个按键的全部 UI 对象（左/中/右三键共用同一套结构）
typedef struct {
    lv_obj_t *cont;       // 容器 60x40
    lv_obj_t *ind;        // 指示器方块 50x20
    lv_obj_t *lbl_status; // 状态文字（"已按下"/"未按下"）
    uint8_t last_state;   // 上一次的状态（状态不变时跳过刷新）
} key_ui_t;

static key_ui_t key_btn_L; // 左键
static key_ui_t key_btn_M; // 中键
static key_ui_t key_btn_R; // 右键

static void key_ui_create(key_ui_t *k, lv_coord_t x, const char *title);
static void key_ui_update(key_ui_t *k, uint8_t cur_state);
static void key_ui_remove(key_ui_t *k);

// 创建单个按键的 UI：容器 + 标题 + 指示器方块 + 状态文字
// x: 容器在屏幕上的横坐标（纵坐标固定 165）
// title: 标题文本（"左键:"/"中键:"/"右键:"）
static void key_ui_create(key_ui_t *k, lv_coord_t x, const char *title)
{
    // 容器 60x40
    k->cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(k->cont, 60, 40);
    lv_obj_set_pos(k->cont, x, 165);
    lv_obj_set_style_pad_all(k->cont, 0, 0);
    lv_obj_set_style_radius(k->cont, 0, 0);
    lv_obj_set_style_bg_opa(k->cont, LV_OPA_0, 0); // 透明
    lv_obj_set_style_border_width(k->cont, 0, 0);
    lv_obj_clear_flag(k->cont, LV_OBJ_FLAG_SCROLLABLE);

    // 标题 Label（左上角）
    lv_obj_t *lbl_title = lv_label_create(k->cont);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_pos(lbl_title, 0, 5);
    lv_obj_set_style_text_font(lbl_title, LV_FONT_DEFAULT, 0); // 默认字体

    // 指示器方块 50x20, 圆角4（默认灰色）
    k->ind = lv_obj_create(k->cont);
    lv_obj_set_size(k->ind, 50, 20);
    lv_obj_align(k->ind, LV_ALIGN_BOTTOM_MID, 0, 0); // 底部居中
    lv_obj_set_style_radius(k->ind, 4, 0);
    lv_obj_set_style_border_width(k->ind, 0, 0);
    lv_obj_set_style_bg_color(k->ind, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
    lv_obj_clear_flag(k->ind, LV_OBJ_FLAG_SCROLLABLE);

    // 状态文字（居中显示）
    k->lbl_status = lv_label_create(k->ind);
    lv_label_set_text(k->lbl_status, "未按下");
    lv_obj_center(k->lbl_status);
}

// 刷新单个按键的 UI：状态变化时才更新颜色与文字（按下淡蓝，未按下淡灰）
static void key_ui_update(key_ui_t *k, uint8_t cur_state)
{
    if(cur_state == k->last_state) return;

    k->last_state = cur_state;
    if(cur_state) {
        lv_obj_set_style_bg_color(k->ind, lv_palette_lighten(LV_PALETTE_BLUE, 1), 0);
        lv_label_set_text(k->lbl_status, "已按下");
    } else {
        lv_obj_set_style_bg_color(k->ind, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
        lv_label_set_text(k->lbl_status, "未按下");
    }
}

// 删除单个按键的 UI 对象并清空指针
static void key_ui_remove(key_ui_t *k)
{
    if(k->cont) {
        lv_obj_del(k->cont); // 指示器与状态文字均为容器子对象，随容器一并删除
        k->cont = NULL;
    }
    k->ind = NULL;
    k->lbl_status = NULL;
}

void Create_Button_Label(void)
{
    key_ui_create(&key_btn_L, 15, "左键:");
    key_ui_create(&key_btn_M, 90, "中键:");
    key_ui_create(&key_btn_R, 165, "右键:");
}

void Update_Button_Label(void)
{
    if(!key_btn_L.cont || !key_btn_M.cont || !key_btn_R.cont) return;

    key_ui_update(&key_btn_L, g_key_L_M_RT);
    key_ui_update(&key_btn_M, g_key_WKP_RT);
    key_ui_update(&key_btn_R, g_key_R_M_RT);
}

void Remove_Button_Label(void)
{
    key_ui_remove(&key_btn_L);
    key_ui_remove(&key_btn_M);
    key_ui_remove(&key_btn_R);
}
