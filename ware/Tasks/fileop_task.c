// 后台文件操作任务 (复制/删除)
// 通过 Task Notification 接收 UI 请求，避免阻塞 LVGL 任务
#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "defines.h"
#include "variables.h"
#include "task_manager.h"
#include "file_unit.h"
#include "malloc.h"

// 记事本写文本文件入口 (实现在 note_edit_unit.c)
extern int do_write_text(const char * path, const char * text);

void FileOp_Task(void *pvParameters)
{
    // 等待 SD 卡初始化完成
    while (!g_TFcard_inited)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    while (1)
    {
        // 阻塞等待 UI 发送任务通知
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint8_t res = FILEOP_RES_FAIL;

        switch (g_file_op_cmd)
        {
            case 1: // 复制
                if (g_async_src && g_async_dst)
                {
                    res = (do_copy(g_async_src, g_async_dst) == 0) ? FILEOP_RES_OK : FILEOP_RES_FAIL;
                }
                // 释放路径缓冲区
                if (g_async_src) { free_bsc(g_async_src); g_async_src = NULL; }
                if (g_async_dst) { free_bsc(g_async_dst); g_async_dst = NULL; }
                break;

            case 2: // 删除
                if (g_async_src)
                {
                    res = (do_delete(g_async_src) == 0) ? FILEOP_RES_OK : FILEOP_RES_FAIL;
                }
                if (g_async_src) { free_bsc(g_async_src); g_async_src = NULL; }
                break;

            case 3: // 写文本文件 (src 是路径, dst 是正文)
                if (g_async_src && g_async_dst)
                {
                    res = (uint8_t)do_write_text(g_async_src, g_async_dst);
                }
                if (g_async_src) { free_bsc(g_async_src); g_async_src = NULL; }
                if (g_async_dst) { free_bsc(g_async_dst); g_async_dst = NULL; }
                break;

            default:
                break;
        }

        g_file_op_result = res;
        g_file_op_busy   = 0;   // 通知 UI 操作完成
        g_file_op_done   = 1;   // 触发 UI 刷新
        g_file_op_cmd    = 0;   // 回到空闲状态
    }
}
