#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "page_manager.h"
#include "start_page.h"
#include "desktop_page.h"
#include "variables.h"
#include <string.h>
#include <stdarg.h>
#include "malloc.h"

// 不受lvgl管理的页面
static const Page_Interface_t page_game_interface = { .id = PAGE_GAME };
static const Page_Interface_t page_media_interface = { .id = PAGE_MEDIA };
static const Page_Interface_t page_display_interface = { .id = PAGE_DISPLAY };


// 1. 静态映射表：直接将页面接口指针按 ID 顺序放入数组
static const Page_Interface_t* const page_registry[PAGE_MAX_ID] = {
    [PAGE_NONE]       = NULL,
    [PAGE_START]      = &page_start_interface,
    [PAGE_DESKTOP]    = &page_desktop_interface,
    [PAGE_FILE]       = NULL,
    [PAGE_FONT_UPDATE]= NULL,
    [PAGE_GAME]       = &page_game_interface,
    [PAGE_MEDIA]      = &page_media_interface,
    [PAGE_DISPLAY]    = &page_display_interface,
    [PAGE_MUSIC]      = NULL
};

// 2. 将状态单独提取出来，放在 SRAM 中
static Page_State_t page_states[PAGE_MAX_ID] = {PAGE_STATE_UNINIT};// 初始化为未初始化状态

static volatile uint32_t current_page_id = PAGE_NONE;// 当前页面ID
static volatile uint32_t next_page_id = PAGE_START;// 下一个页面ID

// 历史记录相关变量
static volatile uint32_t page_history_stack[PAGE_HISTORY_MAX_DEPTH];// 历史记录栈
static volatile uint16_t history_count = 0;// 历史记录栈计数器
static volatile bool is_back_action = false;// 是否是后退操作

// O(1) 的快速查找函数
// page_id : 页面ID
// 返回值 : 对应的页面接口指针，如果不存在则返回 NULL
static inline const Page_Interface_t* Page_Get_Interface(uint32_t page_id) {
    if (page_id < PAGE_MAX_ID) {
        return page_registry[page_id];
    }
    return NULL;
}

// 获取当前页面ID
uint32_t Page_Get_Current(void)
{
    return current_page_id;
}

// 获取即将切换的目标页面ID 
uint32_t Page_Get_Next(void)
{
    return next_page_id;
}


// 初始化
void Page_Manager_Init(void)
{
	if (g_font_need_update)
		Page_Request_Switch(PAGE_FONT_UPDATE);
	else
		Page_Request_Switch(PAGE_START);
}

void _Page_Request_Switch_Impl(uint32_t new_page_id, const char *path, ...)
{
    if (Page_Get_Interface(new_page_id) != NULL) {
        if (new_page_id == PAGE_FILE) {
            if (current_path == NULL) 
			{
				current_path = malloc_bsc(256);
			}
            if (current_path) 
			{
                strncpy(current_path, path ? path : "", 255);
                current_path[255] = '\0';
            }
        }
        next_page_id = new_page_id;
        is_back_action = false;
    }
}

// 触发后退功能
void Page_Back(void)
{
    if (history_count > 0) {
        history_count--;
        next_page_id = page_history_stack[history_count];
        is_back_action = true; 
    } else if (current_page_id > PAGE_DESKTOP) {
        next_page_id = PAGE_DESKTOP;
        is_back_action = true;
    }
}

// 手动推入历史栈（用于 LVGL 被挂起前保存当前页面）
// 用于不在 LVGL 管理的页面中手动保存当前页面状态
void Page_Push_History(uint32_t page_id)
{
    if (page_id > PAGE_DESKTOP && history_count < PAGE_HISTORY_MAX_DEPTH) {
        page_history_stack[history_count++] = page_id;
    }
}

// 清除历史记录
void Page_Clear_History(void)
{
    history_count = 0;
}

// 清除页面管理器的所有状态
void Page_Manager_Deinit(void)
{
    return;
}

// 页面管理主循环
void Page_Manager_Loop(void)
{
    uint32_t target_next_id = next_page_id;// 读取目标页面ID
    bool current_is_back = is_back_action;// 读取是否是后退操作

    if (current_page_id != target_next_id)// 如果当前页面ID与目标页面ID不同，则进行切换
    {
        // 历史记录逻辑
        if (!current_is_back) {
            if (target_next_id > PAGE_DESKTOP) {
                if (current_page_id != 0 && history_count < PAGE_HISTORY_MAX_DEPTH) {
                    page_history_stack[history_count++] = current_page_id;
                }
            } else {
                history_count = 0; // 回到桌面/启动页清空栈
            }
        }
        
        is_back_action = false;

        const Page_Interface_t* current_page = Page_Get_Interface(current_page_id);
        const Page_Interface_t* next_page = Page_Get_Interface(target_next_id);
        
        // 1. 退出当前页面
        if (current_page) {
            if (current_page->exit) {
                current_page->exit(); 
            }
            page_states[current_page_id] = PAGE_STATE_UNINIT; // 修改独立的状态数组
        }
        
        // 2. 初始化新页面
        if (next_page) {
            if (page_states[target_next_id] == PAGE_STATE_UNINIT && next_page->init) {
                next_page->init();
            }
            page_states[target_next_id] = PAGE_STATE_ACTIVE;
        }
        
        current_page_id = target_next_id;
    }
    
    // 4. 执行当前页面的刷新逻辑
    if (current_page_id < PAGE_MAX_ID) {
        const Page_Interface_t* current_page = Page_Get_Interface(current_page_id);
        if (current_page && current_page->update && page_states[current_page_id] == PAGE_STATE_ACTIVE) {
            current_page->update();
        }
    }
}
