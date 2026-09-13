#include "stm32f4xx.h" 
#include "page_manager.h"
#include "start_unit.h"

// 启动页面初始化函数
static void Start_Page_Init(void)
{
    Create_Start_Unit();
}

// 启动页面更新函数
static void Start_Page_Update(void)
{
    Update_Start_Unit();
}

// 启动页面退出函数
static void Start_Page_Exit(void)
{
    Remove_Start_Unit();
}

// 页面接口定义
const Page_Interface_t page_start_interface = {
    .id = PAGE_START,
    .init = Start_Page_Init,
    .update = Start_Page_Update,
    .exit = Start_Page_Exit,
};
