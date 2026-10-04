#include "about_unit.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "malloc.h"
#include "lvgl.h"
#include "page_manager.h"
#include "variables.h"

extern const lv_img_dsc_t brahman_icon;      //142*22，含 1px 透明边距
extern const lv_img_dsc_t brahman_icon_edge; //142*22，与字面同画布的描边环

// 本地静态指针，用于管理该页面的容器生命周期
static lv_obj_t * about_cont = NULL;

/**
 * @brief 创建一行信息（包含左侧红色条和右侧富文本）
 * 
 * @param parent 父对象
 * @param y_ofs  Y轴偏移量
 * @param text   需要显示的带有颜色代码的文本
 */
static void create_info_row(lv_obj_t * parent, lv_coord_t y_ofs, const char * text) 
{
    // 创建左侧 4*16 红色条条
    lv_obj_t * red_bar = lv_obj_create(parent);
    lv_obj_set_size(red_bar, 4, 16);
    lv_obj_set_style_radius(red_bar, 0, 0);                 // 移除圆角
    lv_obj_set_style_border_width(red_bar, 0, 0);           // 移除边框
    lv_obj_set_style_bg_color(red_bar, lv_color_hex(0xFF0000), 0); // 纯红色
    lv_obj_clear_flag(red_bar, LV_OBJ_FLAG_SCROLLABLE);     // 禁用滚动
    lv_obj_align(red_bar, LV_ALIGN_TOP_LEFT, 10, y_ofs);    // 靠左对齐，留出10px边距

    // 创建文本标签
    lv_obj_t * label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, &lv_font_12, 0);      // 设置为 &lv_font_12 字体
    lv_label_set_recolor(label, true);                      // 启用颜色重绘功能
    lv_label_set_text(label, text);
    // Y轴略微向下偏移以居中于16px高度的红色条条
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 20, y_ofs + 2); 
}

void Create_About_Unit(void)
{
    if (about_cont != NULL) {
        return; // 防止重复创建
    }

    // 创建 240*180 的主容器
    about_cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(about_cont, 240, 180);
    lv_obj_center(about_cont);

    // 移除圆角,阴影,内边距,滚动条,边框等效果, 纯白色背景
    lv_obj_set_style_radius(about_cont, 0, 0);
    lv_obj_set_style_shadow_width(about_cont, 0, 0);
    lv_obj_set_style_pad_all(about_cont, 0, 0);
    lv_obj_set_style_border_width(about_cont, 0, 0);
    lv_obj_set_style_bg_color(about_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_clear_flag(about_cont, LV_OBJ_FLAG_SCROLLABLE);

    // 品牌标识：描边环与字面是两张同画布(142*22)的 1bit 掩膜，故共用同一个对齐偏移即可精确套合
    lv_obj_t * img_edge = lv_img_create(about_cont);
    lv_img_set_src(img_edge, &brahman_icon_edge);
    lv_obj_align(img_edge, LV_ALIGN_TOP_MID, 0, 9);
    lv_obj_set_style_img_recolor(img_edge, lv_color_hex(0x0B2A5A), 0); // 描边染成深蓝
    lv_obj_set_style_img_recolor_opa(img_edge, 255, 0);

    // 字面叠在描边之上，染成纯金色
    lv_obj_t * img_icon = lv_img_create(about_cont);
    lv_img_set_src(img_icon, &brahman_icon);
    lv_obj_align(img_icon, LV_ALIGN_TOP_MID, 0, 9);
    lv_obj_set_style_img_recolor(img_icon, lv_color_hex(0xFFD900), 0);
    lv_obj_set_style_img_recolor_opa(img_icon, 255, 0);

    // 读取STM32F4芯片的96位唯一ID
    // 前 32 位为晶圆坐标信息，按 Hex 显示
    uint32_t cpuid_hex = *(uint32_t *)(0x1FFF7A10);
    // 后 64 位为批次号（ASCII），直接用字符指针读取
    uint8_t * lot_ptr = (uint8_t *)(0x1FFF7A14);
    
    char lot_str[9] = {0}; // 8个字符 + 1个字符串结束符
    for(int i = 0; i < 8; i++) {
        // 过滤：仅保留标准可见 ASCII 字符 (32~126)
        if(lot_ptr[i] >= 32 && lot_ptr[i] <= 126) {
            lot_str[i] = lot_ptr[i];
        } else {
            // 如果遇到非打印字符或者 0x00，不显示乱码，可以用空格代替或者跳过
            lot_str[i] = ' '; 
        }
    }
    
    char id_str[64];
    // 格式化输出: 前面16进制，后面接ASCII字符串
    sprintf(id_str, "#808080 设备ID:# #000000 %08X%s#", (unsigned int)cpuid_hex, lot_str);

    // 逐行创建文本，行距20像素、间隙4像素统一节奏，上下边距各10像素对称
    create_info_row(about_cont, 34,  "#808080 设备名称:# #000000 Brahman#");
    create_info_row(about_cont, 54,  id_str);
    create_info_row(about_cont, 74,  "#808080 处理器:# #000000 STM32F405RGT6#");
    create_info_row(about_cont, 94, "#808080 运行内存:# #000000 192KB# #808080 板载储存:# #000000 16MB#");
    create_info_row(about_cont, 114, "#808080 电池容量:# #000000 2000mAh#");
    create_info_row(about_cont, 134, "#808080 软件版本:# #000000 V1.0#");
    create_info_row(about_cont, 154, "#808080 构建:# #000000 ARMCC5#");
}

//此函数会在lvgl任务总每20ms调用一次,注意性能控制.
void Update_About_Unit(void)
{
    // 静态页面无需频繁刷新
}

void Remove_About_Unit(void)
{
    if (about_cont != NULL) {
        lv_obj_del(about_cont); // 删除容器及其所有子对象释放内存
        about_cont = NULL;
    }
}
