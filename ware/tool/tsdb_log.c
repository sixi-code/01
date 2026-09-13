#include "debug.h"
#include "flashdb.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
// TODO: USB CDC 模块未建, usb_printf 未实现, usbd_cdc_conf.h 建好后再恢复
// #include "usbd_cdc_conf.h"
#include "malloc.h"
#include "variables.h"

#define TSDB_QUEUE_SIZE 20 // 环形缓冲区的大小，存储待写入的日志条目数量

typedef struct {
    char text[256];
} tsdb_queue_entry_t;// 用于存储单条日志的结构体

static tsdb_queue_entry_t *tsdb_queue = NULL;// 队列缓冲区，存储待写入的日志条目
static volatile int tsdb_queue_head = 0;// 队列头索引生产者
static volatile int tsdb_queue_tail = 0;// 队列尾索引消费者
static volatile int tsdb_queue_count = 0;// 队列中当前存储的日志条目数量


// 将日志写入环形缓冲区，等待后续写入 FlashDB
void tsdb_printf(const char *format, ...)
{
    if (!tsdb.parent.init_ok) return;// 如果时间戳数据库未初始化，则直接返回
    if (__get_IPSR() != 0) return;// 如果当前处于中断上下文，则直接返回，避免在中断中进行复杂的操作

    if (tsdb_queue == NULL) {
        tsdb_queue = (tsdb_queue_entry_t *)malloc_bsc(sizeof(tsdb_queue_entry_t) * TSDB_QUEUE_SIZE);// 分配环形缓冲区内存
        if (tsdb_queue == NULL) return;
    }

    char buf[256];

    va_list args;
    va_start(args, format);
    int len = vsnprintf(buf, sizeof(buf) - 1, format, args);// 使用 vsnprintf 格式化字符串，确保不会溢出缓冲区
    va_end(args);

    if (len <= 0) return;
    if (len >= sizeof(buf) - 1) len = sizeof(buf) - 1;
    buf[len] = '\0';

    taskENTER_CRITICAL();
    if (tsdb_queue_count < TSDB_QUEUE_SIZE) {
        strncpy(tsdb_queue[tsdb_queue_head].text, buf, 255);
        tsdb_queue[tsdb_queue_head].text[255] = '\0';
        tsdb_queue_head = (tsdb_queue_head + 1) % TSDB_QUEUE_SIZE;// 更新队列头索引，环形缓冲区循环使用
        tsdb_queue_count++;// 增加队列中日志条目数量
    }
    taskEXIT_CRITICAL();
}

// 将环形缓冲区中的日志条目写入 FlashDB
void tsdb_log_flush(void)
{
    if (tsdb_queue == NULL) return;
    if (!tsdb.parent.init_ok) return;

    fdb_time_t ts = tsdb.last_time;

    while (tsdb_queue_count > 0) {
        taskENTER_CRITICAL();
        if (tsdb_queue_count == 0) {
            taskEXIT_CRITICAL();
            break;
        }
        char buf[256];
        strncpy(buf, tsdb_queue[tsdb_queue_tail].text, 255);
        buf[255] = '\0';
        tsdb_queue_tail = (tsdb_queue_tail + 1) % TSDB_QUEUE_SIZE;// 更新队列尾索引，环形缓冲区循环使用
        tsdb_queue_count--;// 减少队列中日志条目数量
        taskEXIT_CRITICAL();

        ts++;// 增加时间戳，确保每条日志都有唯一的时间戳
        struct fdb_blob blob;
        fdb_tsl_append_with_ts(&tsdb, fdb_blob_make(&blob, buf, strlen(buf)), ts);
    }
}

// 选择输出目标
typedef enum {
    TSDB_OUT_LVGL = 0,
    TSDB_OUT_USB
} tsdb_out_target_t;

// 用于存储单条日志的结构体
typedef struct {
    uint32_t timestamp;
    char text[256];
} log_record_t;

// 用于读取日志的上下文结构体，存储读取状态和缓冲区
struct tsdb_read_ctx {
    int max_count;        // 请求获取的最大条数
    int current_count;    // 实际已经读取的条数
    log_record_t *records;// 临时缓存数组
};

// 将 Unix 时间戳转换为 HH:MM:SS 字符串（考虑时区偏移）
static void timestamp_to_hms(uint32_t timestamp, char *buf, size_t buf_len)
{
    uint32_t local_seconds = timestamp % 86400;

    uint8_t hour = local_seconds / 3600;
    uint8_t minute = (local_seconds % 3600) / 60;
    uint8_t second = local_seconds % 60;
    snprintf(buf, buf_len, "%02d:%02d:%02d", hour, minute, second);
}

// 逆向遍历的回调：将数据存入缓冲，但不直接打印
// tsl: 当前遍历到的时间序列日志节点
// arg: 指向 tsdb_read_ctx 结构体的指针，用于存储
// 返回 true 表示停止遍历，返回 false 表示继续遍历
static bool tsdb_read_cb(fdb_tsl_t tsl, void *arg)
{
    // arg 是指向 tsdb_read_ctx 结构体的指针
    struct tsdb_read_ctx *ctx = (struct tsdb_read_ctx *)arg;
    // 如果已经读取的条数达到请求的最大条数，则停止遍历
    if (ctx->current_count >= ctx->max_count) return true; // 读够了就停止

    struct fdb_blob blob;
    // 获取当前记录对应的缓冲指针
    log_record_t *record = &ctx->records[ctx->current_count];
    // 将时间戳和正文初始化
    record->timestamp = tsl->time;
    // 清空正文缓冲区，确保没有残留数据
    memset(record->text, 0, sizeof(record->text));

    // 从 FlashDB 中读取日志正文
    fdb_tsl_to_blob(tsl, fdb_blob_make(&blob, record->text, sizeof(record->text)));
    size_t read_len = fdb_blob_read((fdb_db_t)&tsdb, &blob);

    if (read_len > 0) {
        if (read_len >= sizeof(record->text)) read_len = sizeof(record->text) - 1;
        record->text[read_len] = '\0';

        // 清洗换行符
        for(int i = 0; i < read_len; i++) {
            if(record->text[i] == '\r' || record->text[i] == '\n') record->text[i] = ' ';
        }

        ctx->current_count++;
    }
    return false; // 继续遍历下一条
}

// 统一的处理函数：获取并正序打印到指定目标（LVGL 或 USB）
// num: 请求获取的最大条数
// target: 输出目标，TSDB_OUT_LVGL 或 TSDB_OUT_USB
static void tsdb_show_recent_forward(int num, tsdb_out_target_t target)
{
    if (!tsdb.parent.init_ok || num <= 0) return;

    // 使用 CCM 内存临时分配日志缓冲，50条约为 50 * 260 = 13000 Bytes
    log_record_t *records = (log_record_t *)malloc_ccm(sizeof(log_record_t) * num);
    if (records == NULL) {
        // 如果 CCM 内存分配失败，直接返回
        return;
    }

    struct tsdb_read_ctx ctx = { .max_count = num, .current_count = 0, .records = records };

    // 逆向查询出最新的 current_count 条日志（此时 records[0] 是最新，records[n] 是最旧）
    fdb_tsl_iter_reverse(&tsdb, tsdb_read_cb, &ctx);

    // 反向遍历输出（实现正序输出：最旧的先输出，最新的最后输出）
    for (int i = ctx.current_count - 1; i >= 0; i--) {
        char time_str[9]; // "HH:MM:SS" + '\0'
        timestamp_to_hms(records[i].timestamp, time_str, sizeof(time_str));

        if (target == TSDB_OUT_LVGL) {
            lvgl_printf("[%s] %s\n", time_str, records[i].text);
            vTaskDelay(pdMS_TO_TICKS(5));
        } else if (target == TSDB_OUT_USB) {
            
            // usb_printf("[%s] %s\r\n", time_str, records[i].text);
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    // 打印完毕，释放 CCM 内存
    free_ccm(records);
}

// 将 TSDB 中的日志信息输出到 LVGL
void tsdb_show_recent_on_lvgl(int num)
{
    tsdb_show_recent_forward(num, TSDB_OUT_LVGL);
}
// 将 TSDB 中的日志信息输出到 USB
void tsdb_show_recent_on_usb(int num)
{
    tsdb_show_recent_forward(num, TSDB_OUT_USB);
}
