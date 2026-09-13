#ifndef __DEBUG_H__
#define __DEBUG_H__

#include <stdint.h>
#include "tsdb_log.h"
#include "debug_unit.h"   // lvgl_printf (Debug_Mode_LVGL 分支用), 不 include 会隐式声明
// TODO: USB CDC 模块未建, usb_printf 未实现, usbd_cdc_conf.h 建好后再恢复
// #include "usbd_cdc_conf.h"
#include "variables.h"
#include "defines.h"

#define debug_printf(format, ...) do {                                  \
    if (kv_debug_mode == Debug_Mode_None) break;                        \
    switch (kv_debug_mode) {                                            \
        case Debug_Mode_TSDB:                                           \
            tsdb_printf(format, ##__VA_ARGS__);                         \
            break;                                                      \
        case Debug_Mode_USBD:                                           \
            /* TODO: usb_printf 未实现, USB CDC 建好后恢复 */            \
            /* usb_printf(format, ##__VA_ARGS__); */                    \
            break;                                                      \
        case Debug_Mode_LVGL:                                           \
            lvgl_printf(format, ##__VA_ARGS__);                         \
            break;                                                      \
        default:                                                        \
            break;                                                      \
    }                                                                   \
} while(0)

// 历史数据导出 (Debug_Mode_None 模式下调用)
void Debug_Dump_TSDB_To_USB(int count);
void Debug_Dump_TSDB_To_LVGL(int count);

#endif
