#include "stm32f4xx.h"
#include "debug.h"
#include "variables.h"
#include "defines.h"

// 将 TSDB 中的日志信息输出到 USB
void Debug_Dump_TSDB_To_USB(int count)
{
    if (kv_debug_mode != Debug_Mode_None) return;
    // TODO: usb_printf 未实现, USB CDC 建好后恢复
    // usb_printf("--- TSDB Dump Start (%d) ---\r\n", count);
    tsdb_show_recent_on_usb(count);
    // usb_printf("--- TSDB Dump End ---\r\n");
}

// 将 TSDB 中的日志信息输出到 LVGL
void Debug_Dump_TSDB_To_LVGL(int count)
{
    if (kv_debug_mode != Debug_Mode_None) return;
    lvgl_printf("--- TSDB Dump Start (%d) ---\n", count);
    tsdb_show_recent_on_lvgl(count);
    lvgl_printf("--- TSDB Dump End ---\n");
}
