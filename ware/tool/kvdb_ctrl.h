#ifndef __KVDB_CTRL_H__
#define __KVDB_CTRL_H__

#include "stm32f4xx.h"

// 添加需要持久化的参数：KV(变量名, 类型)
// 注意：仅列出已在 variables.c 中定义的变量，未引入的变量待定义后再添加
#define PERSIST_LIST \
    KV(kv_hdp0_or_spk1,        uint8_t) \
    KV(kv_hdp_value,           uint8_t)


// 自动生成索引枚举
#define KV(name, type) KV_IDX_##name,
enum { PERSIST_LIST PERSIST_COUNT };
#undef KV

void kvdb_persist_load(void);
void kvdb_persist_mark(int index);
void kvdb_persist_flush(void);

#endif