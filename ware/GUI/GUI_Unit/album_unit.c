#include "album_unit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "FreeRTOS.h"
#include "task.h"
#include "malloc.h"
#include "lvgl.h"
#include "page_manager.h"
#include "variables.h"
#include "defines.h"
#include "task_manager.h"
#include "ff.h" 
#include "fatfs.h" 

#define ALBUM_DIR_PATH "0:/ALBUM"

// 页面状态结构体
typedef struct {
    lv_obj_t * main_cont;   // 主容器对象
    lv_obj_t * img_obj;     // 当前显示的图像对象
    lv_obj_t * info_label;  // 无法页内预览的文件在此显示文件名与提示
    
    char ** img_paths;      // 保存所有文件路径的动态数组
    uint16_t item_cnt;      // 文件总数
    uint16_t item_cap;      // 数组容量
    int16_t current_idx;    // 当前正在显示的文件索引
} album_state_t;

static album_state_t * album_state = NULL;

// 判断后缀是否属于相册放行的媒体文件
// 清单与媒体任务(media_task.c)的分派保持一致，确保选中后交给它一定能被正确解码
static uint8_t album_ext_supported(const char *name)
{
    uint8_t * ext = fatfs_get_extension(name);

    return (strcmp((char *)ext, "bmp")   == 0) ||
           (strcmp((char *)ext, "jpg")   == 0) ||
           (strcmp((char *)ext, "jpeg")  == 0) ||
           (strcmp((char *)ext, "png")   == 0) ||
           (strcmp((char *)ext, "gif")   == 0) ||
           (strcmp((char *)ext, "mjpeg") == 0) ||
           (strcmp((char *)ext, "avi")   == 0) ||
           (strcmp((char *)ext, "raw")   == 0);
}

// 加载当前索引的文件
static void load_current_image(void)
{
    if (!album_state || album_state->item_cnt == 0) return;

    const char * path = album_state->img_paths[album_state->current_idx];

    // 页内只打开了 BMP 解码器，可直接预览；其余后缀页内无解码器，显示文件名待「确定」播放
    uint8_t * ext = fatfs_get_extension(path);
    if (strcmp((char *)ext, "bmp") == 0) {
        if (album_state->img_obj) {
            lv_obj_clear_flag(album_state->img_obj, LV_OBJ_FLAG_HIDDEN);
            lv_img_set_src(album_state->img_obj, path);
        }
        if (album_state->info_label) lv_obj_add_flag(album_state->info_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (album_state->img_obj) {
            lv_img_set_src(album_state->img_obj, NULL);
            lv_obj_add_flag(album_state->img_obj, LV_OBJ_FLAG_HIDDEN);
        }
        if (album_state->info_label) {
            const char * name = strrchr(path, '/');
            lv_label_set_text(album_state->info_label, name ? name + 1 : path);
            lv_obj_clear_flag(album_state->info_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// 中间区域点击回调：把当前选中的文件交给媒体任务播放
static void play_click_cb(lv_event_t * e)
{
    if (!album_state || album_state->item_cnt == 0) return;

    // 选中文件的完整路径交给媒体任务(与文件浏览器同一套交接方式)
    if (chosen_file_path == NULL) chosen_file_path = malloc_bsc(256);
    if (chosen_file_path == NULL) return;

    strncpy(chosen_file_path, album_state->img_paths[album_state->current_idx], 255);
    chosen_file_path[255] = '\0';
    g_file_chosen = 1; // 交给 Update_Album_Unit 创建媒体任务
}

// 左右点击切换区域的事件回调
static void nav_click_cb(lv_event_t * e)
{
    if (!album_state || album_state->item_cnt == 0) return;
    
    // 获取传入的自定义数据，-1 表示上一张(左边)，1 表示下一张(右边)
    int direction = (int)(intptr_t)lv_event_get_user_data(e);
    
    if (direction == -1) {
        album_state->current_idx--;
        if (album_state->current_idx < 0) {
            album_state->current_idx = album_state->item_cnt - 1; // 循环到最后一张
        }
    } else {
        album_state->current_idx++;
        if (album_state->current_idx >= album_state->item_cnt) {
            album_state->current_idx = 0; // 循环到第一张
        }
    }
    
    load_current_image();
}

void Create_Album_Unit(void)
{
    if (album_state != NULL) return;

    album_state = (album_state_t *)malloc_bsc(sizeof(album_state_t));
    if (!album_state) return;
    memset(album_state, 0, sizeof(album_state_t));

    // 初始化路径数组
    album_state->item_cap = 16;
    album_state->item_cnt = 0;
    album_state->current_idx = 0;
    album_state->img_paths = malloc_bsc(sizeof(char*) * album_state->item_cap);

    // 遍历 SD 卡获取所有受支持的媒体文件路径
    DIR dir;
    FILINFO fno;
    FRESULT res = f_opendir(&dir, ALBUM_DIR_PATH);
    if (res == FR_OK) {
        while (1) {
            res = f_readdir(&dir, &fno);
            if (res != FR_OK || fno.fname[0] == 0) break; 
            if (fno.fattrib & (AM_HID | AM_DIR)) continue;

            if (album_ext_supported(fno.fname)) {
                // 扩容判断
                if (album_state->item_cnt >= album_state->item_cap) {
                    album_state->item_cap *= 2;
                    char ** new_paths = malloc_bsc(sizeof(char*) * album_state->item_cap);
                    memcpy(new_paths, album_state->img_paths, sizeof(char*) * album_state->item_cnt);
                    free_bsc(album_state->img_paths);
                    album_state->img_paths = new_paths;
                }

                char * full_path = malloc_bsc(128); 
                if (full_path) {
                    snprintf(full_path, 128, "%s/%s", ALBUM_DIR_PATH, fno.fname);
                    album_state->img_paths[album_state->item_cnt] = full_path;
                    album_state->item_cnt++;
                }
            }
        }
        f_closedir(&dir);
    }

    // 创建主容器 240x180
    album_state->main_cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(album_state->main_cont, 240, 180);
    lv_obj_center(album_state->main_cont);
    lv_obj_set_style_bg_color(album_state->main_cont, lv_color_hex(0x000000), 0); // 黑色底色
    lv_obj_set_style_border_width(album_state->main_cont, 0, 0);
    lv_obj_set_style_radius(album_state->main_cont, 0, 0); 
    lv_obj_set_style_pad_all(album_state->main_cont, 0, 0);       // 取消内边距，确信占满
    lv_obj_clear_flag(album_state->main_cont, LV_OBJ_FLAG_SCROLLABLE); // 禁止滑动
    // LVGL v8 中超出父容器范围的子对象会自动被裁剪隐藏，不需要额外设置clip，pad_all为0即可

    // 创建图片对象 (居中显示，不做任何缩放)
    album_state->img_obj = lv_img_create(album_state->main_cont);
    lv_obj_align(album_state->img_obj, LV_ALIGN_CENTER, 0, 0);

    // 创建文件名提示标签 (仅对无法页内预览的文件显示)
    album_state->info_label = lv_label_create(album_state->main_cont);
    lv_label_set_long_mode(album_state->info_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(album_state->info_label, 200);
    lv_obj_set_style_text_color(album_state->info_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(album_state->info_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(album_state->info_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(album_state->info_label, LV_OBJ_FLAG_HIDDEN);

    // 创建左右隐形点击区域 (60x180)
    // 左边点击区域 (上一个)
    lv_obj_t * left_area = lv_obj_create(album_state->main_cont);
    lv_obj_set_size(left_area, 60, 180);
    lv_obj_align(left_area, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_opa(left_area, 0, 0);           // 背景完全透明
    lv_obj_set_style_border_width(left_area, 0, 0);
    lv_obj_clear_flag(left_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(left_area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(left_area, nav_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);

    // 右边点击区域 (下一个)
    lv_obj_t * right_area = lv_obj_create(album_state->main_cont);
    lv_obj_set_size(right_area, 60, 180);
    lv_obj_align(right_area, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_opa(right_area, 0, 0);          // 背景完全透明
    lv_obj_set_style_border_width(right_area, 0, 0);
    lv_obj_clear_flag(right_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(right_area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(right_area, nav_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);

    // 中间隐形点击区域 (120x180) —— 确定键, 交给媒体任务播放当前文件
    lv_obj_t * play_area = lv_obj_create(album_state->main_cont);
    lv_obj_set_size(play_area, 120, 180);
    lv_obj_align(play_area, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(play_area, 0, 0);           // 背景完全透明
    lv_obj_set_style_border_width(play_area, 0, 0);
    lv_obj_clear_flag(play_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(play_area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(play_area, play_click_cb, LV_EVENT_CLICKED, NULL);

    // 显示第一个文件
    if (album_state->item_cnt > 0) {
        load_current_image();
    } else {
        // 如果没有文件，可以在这里显示一个提示语
        lv_obj_t * empty_label = lv_label_create(album_state->main_cont);
        lv_label_set_text(empty_label, "No Media");
        lv_obj_set_style_text_color(empty_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(empty_label, LV_ALIGN_CENTER, 0, 0);
    }
}

void Update_Album_Unit(void)
{
    // 相册里按「确定」选中文件后, 创建媒体任务播放它(复用已有的媒体任务逻辑)
    if (g_file_chosen)
    {
        g_file_chosen = 0;
        Taskmanager_Ctrl(Task_N_Media, Task_T_Creat, 0);
    }
}

void Remove_Album_Unit(void)
{
    if (album_state) {
        if (album_state->main_cont) {
            lv_obj_del(album_state->main_cont);
        }
        
        // 释放图片路径数组内存
        if (album_state->img_paths) {
            for(uint16_t i = 0; i < album_state->item_cnt; i++) {
                if (album_state->img_paths[i]) {
                    free_bsc(album_state->img_paths[i]);
                }
            }
            free_bsc(album_state->img_paths);
        }

        free_bsc(album_state);
        album_state = NULL;
    }
}
