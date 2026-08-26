#include "stm32f4xx.h"
#include "malloc.h"

// ===== 极简 bump allocator（仅为阶段链接通过，后期换 TLSF）=====

// CCM 池：0x100006C0 起，约 62KB（与模板 TLSF_CCM_SIZE 一致）
#define CCM_POOL_START 0x100006C0
#define CCM_POOL_SIZE  (64 * 1024 - 864 * 2)

// BSC 池：主 SRAM 末端 98KB（实际地址由链接脚本决定，这里用静态数组模拟）
#define BSC_POOL_SIZE (98 * 1024)

static uint8_t  ccm_pool[CCM_POOL_SIZE];
static uint8_t  bsc_pool[BSC_POOL_SIZE];
static uint32_t ccm_offset = 0;
static uint32_t bsc_offset = 0;

void *malloc_ccm(uint32_t bytes)
{
    // 4 字节对齐
    bytes = (bytes + 3) & ~3;
    if (ccm_offset + bytes > CCM_POOL_SIZE) return NULL;
    void *p = &ccm_pool[ccm_offset];
    ccm_offset += bytes;
    return p;
}

void free_ccm(void* ptr) { (void)ptr; /* bump allocator 不支持 free */ }

void *malloc_bsc(uint32_t bytes)
{
    bytes = (bytes + 3) & ~3;
    if (bsc_offset + bytes > BSC_POOL_SIZE) return NULL;
    void *p = &bsc_pool[bsc_offset];
    bsc_offset += bytes;
    return p;
}

void free_bsc(void* ptr) { (void)ptr; }

void *realloc_bsc(void* ptr, uint32_t bytes) { (void)ptr; (void)bytes; return NULL; }
void *realloc_ccm(void* ptr, uint32_t bytes) { (void)ptr; (void)bytes; return NULL; }

uint8_t tlsf_init(void) { return 0; }
void tlsf_monitor_bsc(mem_monitor_t* mon) { (void)mon; }
void tlsf_monitor_ccm(mem_monitor_t* mon) { (void)mon; }
void tlsf_monitor_all(mem_monitor_t* mon) { (void)mon; }