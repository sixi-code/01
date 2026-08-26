#ifndef MALLOC_H
#define MALLOC_H

#include <stdint.h>
#include <stddef.h>

// TLSF 内存监控结构（占位，2.12 完善）
typedef struct {
    uint32_t total_size;
    uint32_t free_cnt;
    uint32_t free_size;
    uint32_t free_biggest_size;
    uint32_t used_cnt;
    uint32_t used_size;
    uint8_t  used_pct;
    uint8_t  frag_pct;
} mem_monitor_t;

// FreeRTOSConfig.h 用到的两个符号（重定向到 CCM 池）
void *malloc_ccm(uint32_t bytes);
void free_ccm(void* ptr);

// 占位，2.12 TLSF 实现时再补全
void *malloc_bsc(uint32_t bytes);
void free_bsc(void* ptr);
void *realloc_bsc(void* ptr, uint32_t bytes);
void *malloc_ccm(uint32_t bytes);
void free_ccm(void* ptr);
void *realloc_ccm(void* ptr, uint32_t bytes);

uint8_t tlsf_init(void);
void tlsf_monitor_bsc(mem_monitor_t* mon);
void tlsf_monitor_ccm(mem_monitor_t* mon);
void tlsf_monitor_all(mem_monitor_t* mon);

#endif