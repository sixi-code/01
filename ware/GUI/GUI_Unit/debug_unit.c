#include "debug_unit.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "malloc.h"
#include "lvgl.h"
#include "page_manager.h"
#include "variables.h"
#include "debug.h"
#include "kvdb_ctrl.h"

// 日志配置参数
#define MAX_LOG_LINES 52// 最大显示行数
#define MAX_LOG_LEN   256// 每行最大长度

static lv_obj_t *log_cont = NULL;// 日志容器对象
static lv_obj_t **log_labels = NULL;// 日志标签对象数组

static char (*log_buffer)[MAX_LOG_LEN] = NULL;// 日志缓冲区指针，环形缓冲区，每行最大长度为 MAX_LOG_LEN

static uint16_t log_head = 0;// 环形缓冲区头索引，指向最新写入的日志行
static uint16_t log_count = 0;// 当前日志行数，最大为 MAX_LOG_LINES
static uint8_t  need_update = 0;// 是否需要更新显示，0 表示不需要，1 表示需要

volatile uint8_t tsdb_lvgl_need_dump = 0;// 是否需要将 TSDB 日志输出到 LVGL，0 表示不需要，1 表示需要

// 按钮事件回调函数，点击返回按钮时切换到日志控制页面
static void btn_back_event_cb(lv_event_t * e)
{   
    kv_debug_mode = Debug_Mode_None;
    kvdb_persist_mark(KV_IDX_kv_debug_mode);
    Page_Request_Switch(PAGE_LOG_CTRL);
}

// 创建调试单元，初始化日志显示容器和标签
void Create_Debug_Unit(void)
{   
    if (log_cont != NULL) return;
    
    // 分配日志标签数组和日志缓冲区
    if (log_labels == NULL) {
        log_labels = malloc_ccm(MAX_LOG_LINES * sizeof(lv_obj_t *));
    }
    if (log_buffer == NULL) {
        log_buffer = malloc_ccm(MAX_LOG_LINES * MAX_LOG_LEN);
    }
    if (log_labels == NULL || log_buffer == NULL) {
        Remove_Debug_Unit();
        return;
    }

    log_head = 0;
    log_count = 0;
    need_update = 0;
    memset(log_buffer, 0, MAX_LOG_LINES * MAX_LOG_LEN);

    log_cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(log_cont, 240, 180);
    lv_obj_center(log_cont);
    lv_obj_set_scrollbar_mode(log_cont, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_all(log_cont, 5, LV_PART_MAIN);

    lv_obj_set_style_radius(log_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(log_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(log_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(log_cont, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_set_flex_flow(log_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(log_cont, 0, LV_PART_MAIN);

    for (int i = 0; i < MAX_LOG_LINES; i++) {
        log_labels[i] = lv_label_create(log_cont);
        lv_obj_set_style_text_font(log_labels[i], &lv_font_12, 0);

        lv_obj_set_height(log_labels[i], 16);
        lv_obj_set_width(log_labels[i], LV_SIZE_CONTENT);
        
        lv_label_set_text(log_labels[i], "");

        lv_obj_add_flag(log_labels[i], LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t * btn_back = lv_btn_create(log_cont);
    lv_obj_set_size(btn_back, 20, 20);
    lv_obj_add_flag(btn_back, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(btn_back, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_set_style_pad_all(btn_back, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn_back, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_back, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN);

    extern const lv_img_dsc_t exit_icon;
    lv_obj_t * lbl_back = lv_img_create(btn_back);
    lv_img_set_src(lbl_back, &exit_icon);
    lv_obj_center(lbl_back);
    lv_obj_set_style_img_recolor(lbl_back, lv_color_black(), 0);
    lv_obj_set_style_img_recolor_opa(lbl_back, LV_OPA_COVER, 0);

    lv_obj_add_event_cb(btn_back, btn_back_event_cb, LV_EVENT_CLICKED, NULL);
    
    // 如果需要，将 TSDB 日志输出到 LVGL
    if (tsdb_lvgl_need_dump) {
        tsdb_lvgl_need_dump = 0;
        Debug_Dump_TSDB_To_LVGL(30);
    }
}

// 更新调试单元，刷新日志显示
void Update_Debug_Unit(void)
{
    if (!need_update || log_cont == NULL || log_buffer == NULL) return;

    need_update = 0;

    int start_idx = (log_count == MAX_LOG_LINES) ? log_head : 0;
    // 将环形缓冲区中的日志行显示到标签上
    for (int i = 0; i < log_count; i++) {
        int idx = (start_idx + i) % MAX_LOG_LINES;
        lv_label_set_text_static(log_labels[i], log_buffer[idx]);
        lv_obj_clear_flag(log_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
    // 隐藏未使用的标签
    for (int i = log_count; i < MAX_LOG_LINES; i++) {
        lv_obj_add_flag(log_labels[i], LV_OBJ_FLAG_HIDDEN);
    }

    if (lv_obj_get_state(log_cont) & LV_STATE_PRESSED) {
        return;
    }
    // 滚动到最新日志行
    lv_obj_scroll_to_y(log_cont, LV_COORD_MAX, LV_ANIM_OFF);
    lv_obj_scroll_to_x(log_cont, 0, LV_ANIM_OFF);
}

// 移除调试单元
void Remove_Debug_Unit(void)
{
    if (log_cont != NULL) {
        lv_obj_del(log_cont);
        log_cont = NULL;
        if (log_labels != NULL) {
            memset(log_labels, 0, MAX_LOG_LINES * sizeof(lv_obj_t *));
        }
    }

    if (log_labels != NULL) {
        free_ccm(log_labels);
        log_labels = NULL;
    }

    if (log_buffer != NULL) {
        free_ccm(log_buffer);
        log_buffer = NULL;
    }
}

// 将一行文本写入环形缓冲区
// line: 指针，指向要写入的文本行
static void lvgl_log_write_line(const char *line)
{
    strncpy(log_buffer[log_head], line, MAX_LOG_LEN - 1);
    log_buffer[log_head][MAX_LOG_LEN - 1] = '\0';
    log_head = (log_head + 1) % MAX_LOG_LINES;
    if (log_count < MAX_LOG_LINES) log_count++;
}

// 将格式化的日志信息输出到 LVGL
// format: 格式化字符串，类似于 printf 的格式
void lvgl_printf(const char *format, ...)
{   // 如果未初始化日志容器或缓冲区，或者当前处于中断上下文，则直接返回
    if (log_cont == NULL || log_buffer == NULL) return;
    if (__get_IPSR() != 0) return;

    char buf[MAX_LOG_LEN];// 临时缓冲区，用于存储格式化后的日志信息

    // 使用可变参数处理格式化字符串
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buf, sizeof(buf) - 1, format, args);
    va_end(args);

    if (len <= 0) return;

    if (len >= sizeof(buf) - 1) len = sizeof(buf) - 1;
    buf[len] = '\0';

    // 压缩掉 \r，按 \n 拆分为多行
    int w = 0;
    for (int i = 0; i < len; i++) {
        if (buf[i] != '\r') buf[w++] = buf[i];
    }
    len = w;
    buf[len] = '\0';

    int line_start = 0;
    for (int i = 0; i <= len; i++) {
        if (buf[i] == '\n' || buf[i] == '\0') {
            buf[i] = '\0';
            if (i > line_start) {
                lvgl_log_write_line(&buf[line_start]);
            }
            line_start = i + 1;
            if (buf[i] == '\0') break;
        }
    }
    need_update = 1;
}

