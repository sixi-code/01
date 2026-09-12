#ifndef __KVDB_CTRL_H__
#define __KVDB_CTRL_H__

#include "stm32f4xx.h"

// 添加需要持久化的参数：KV(变量名, 类型)
// 注意：仅列出已在 variables.c 中定义的变量；新增变量请同时写入本表（KV_IDX_* 由本表自动生成）
#define PERSIST_LIST \
    KV(kv_hdp0_or_spk1,        uint8_t) \
    KV(kv_hdp_value,           uint8_t) \
    KV(kv_es9018_volume,       uint8_t) \
    KV(kv_es9018_cfg,  ES9018_Config_t) \
    KV(kv_debug_mode,          uint8_t) \
    KV(kv_brightness,          uint8_t) \
    KV(kv_screen_status,       uint8_t) \
    KV(kv_es9018_status,       uint8_t) \
    KV(kv_max98357_ststus,     uint8_t) \
    KV(kv_spk_value,           uint8_t)


// 自动生成索引枚举
#define KV(name, type) KV_IDX_##name,
enum { PERSIST_LIST PERSIST_COUNT };
#undef KV

void kvdb_persist_load(void);
void kvdb_persist_mark(int index);
void kvdb_persist_flush(void);

#endif