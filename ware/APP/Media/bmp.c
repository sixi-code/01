#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "ff.h"
#include "lcd_bsp.h"
#include "malloc.h"
#include <string.h>
#include <stdlib.h>
#include "task_manager.h"
#include "lvgl.h"
#include "variables.h"
#include "defines.h"

#define LCD_WIDTH  240 // LCD 宽度
#define LCD_HEIGHT 240 // LCD 高度
#define BUF_LINES  16 // 缓冲行数

typedef __packed struct {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
} BMP_FILE_HEADER;

typedef __packed struct {
    uint32_t biSize;
    int32_t  biWidth;
    int32_t  biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} BMP_INFO_HEADER;

// 解码 BMP 图片并显示在 LCD 上
// path: BMP 文件路径
uint8_t Decode_BMP_Picture(const char *path)
{
    FIL fil;// 文件对象
    FRESULT fr;// 文件操作结果
    UINT br;// 读取的字节数
    uint8_t ret = 0;// 返回值，0表示成功，1表示失败
    uint8_t file_opened = 0;// 文件是否已打开标志
    
    uint16_t *line_buf = NULL;// LCD 缓冲区
    uint8_t *src_buf = NULL;// BMP 源数据缓冲区

    BMP_FILE_HEADER file_h;// BMP 文件头
    BMP_INFO_HEADER info_h;// BMP 信息头

    do {
        fr = f_open(&fil, path, FA_READ);
        if (fr != FR_OK) {
            ret = 1;
            break;
        }
        file_opened = 1;

        f_read(&fil, &file_h, sizeof(BMP_FILE_HEADER), &br);
        f_read(&fil, &info_h, sizeof(BMP_INFO_HEADER), &br);

        // 检查 BMP 文件类型和位深度
        if (file_h.bfType != 0x4D42 || info_h.biBitCount < 16) { 
            ret = 1;
            break;
        }
        
        uint32_t w = info_h.biWidth;// BMP 宽度
        uint32_t h = abs(info_h.biHeight);// BMP 高度
        uint8_t bottom_up = (info_h.biHeight > 0);// BMP 图像数据是否自下而上存储

        // 计算缩放比例和裁剪区域
        uint32_t short_side = (w < h) ? w : h;// 取短边
        uint8_t scale = 1;// 缩放比例，初始为 1
        while (scale < 8 && (short_side / (scale * 2)) >= LCD_WIDTH) {
            scale *= 2;
        }

        int32_t scaled_w = w / scale;// 缩放后的宽度
        int32_t scaled_h = h / scale;// 缩放后的高度

        int32_t offset_x = (LCD_WIDTH - scaled_w) / 2;// 水平偏移量
        int32_t offset_y = (LCD_HEIGHT - scaled_h) / 2;// 垂直偏移量

        int32_t crop_left   = (offset_x < 0) ? 0 : offset_x;// 裁剪左边界
        int32_t crop_right  = (offset_x + scaled_w > LCD_WIDTH) ? LCD_WIDTH - 1 : offset_x + scaled_w - 1;// 裁剪右边界
        int32_t crop_top    = (offset_y < 0) ? 0 : offset_y;// 裁剪上边界
        int32_t crop_bottom = (offset_y + scaled_h > LCD_HEIGHT) ? LCD_HEIGHT - 1 : offset_y + scaled_h - 1;// 裁剪下边界

        line_buf = (uint16_t*)malloc_bsc(LCD_WIDTH * BUF_LINES * sizeof(uint16_t));
        
        uint8_t bytes_per_pixel = info_h.biBitCount / 8; // 每个像素的字节数
        uint32_t row_bytes = ((w * info_h.biBitCount + 31) / 32) * 4; // 每行字节数（按 4 字节对齐）
        uint32_t pixels_to_read = (crop_right - crop_left + 1) * scale;// 每行需要读取的像素数
        uint32_t bytes_to_read = pixels_to_read * bytes_per_pixel;// 每行需要读取的字节数
        src_buf = (uint8_t*)malloc_bsc(bytes_to_read);

        // 检查内存分配是否成功
        if (!line_buf || !src_buf) {
            ret = 1;
            break;
        }

        uint16_t buf_filled = 0;// 缓冲区已填充的行数
        uint16_t buf_start_y = crop_top;// 缓冲区起始行号
        memset(line_buf, 0, LCD_WIDTH * BUF_LINES * sizeof(uint16_t));// 清空缓冲区
        
        // 遍历裁剪区域的每一行
        for (int32_t y = crop_top/* 裁剪区域的起始行 */; y <= crop_bottom/* 裁剪区域的结束行 */; y++) {
            int32_t img_y = y - offset_y;// 计算对应的 BMP 图像行号
            int32_t src_y = img_y * scale;// 计算对应的 BMP 源数据行号
            
            // 如果 BMP 图像数据自下而上存储，则需要调整源数据行号
            if (bottom_up) {
                src_y = h - 1 - src_y; 
            }

            int32_t img_x_start = crop_left - offset_x;// 计算裁剪区域的起始列在 BMP 图像中的位置
            int32_t src_x_start = img_x_start * scale;// 计算裁剪区域的起始列在 BMP 源数据中的位置

            uint32_t file_offset = file_h.bfOffBits + src_y * row_bytes + src_x_start * bytes_per_pixel;// 计算文件偏移量
            if (f_lseek(&fil, file_offset) == FR_OK) {
                f_read(&fil, src_buf, bytes_to_read, &br);
                
                uint16_t *dst = &line_buf[buf_filled * LCD_WIDTH + crop_left];// 计算缓冲区中当前行的起始位置
                uint8_t *src_ptr = src_buf;// 指向源数据缓冲区的指针

                for (int32_t x = crop_left; x <= crop_right; x++) {
                    // 每个像素的RGB值
                    uint16_t color = 0;
                    // 根据位深度解析像素数据
                    if (bytes_per_pixel == 3 || bytes_per_pixel == 4) {
                        uint8_t b = src_ptr[0];
                        uint8_t g = src_ptr[1];
                        uint8_t r = src_ptr[2];
                        color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
                    } 
                    else if (bytes_per_pixel == 2) {
                        // 16位颜色格式
                        color = src_ptr[0] | (src_ptr[1] << 8); 
                    }
                    *dst = color;
                    dst++;
                    // 移动到下一个像素
                    src_ptr += scale * bytes_per_pixel; 
                }
            }
            // 缓冲区已填充的行数加一
            buf_filled++;
            
            // 如果缓冲区已满或到达裁剪区域的底部，则发送数据到 LCD
            if (buf_filled >= BUF_LINES || y == crop_bottom) {
                LCD_Address_Set(0, buf_start_y, LCD_WIDTH - 1, buf_start_y + buf_filled - 1);
                LCD_Write_DMA(line_buf, LCD_WIDTH * buf_filled);
                xEventGroupWaitBits(xLcdEventGroup, LCD_USER_MDIA, pdTRUE, pdFALSE, portMAX_DELAY);
                
                // 重置缓冲区状态
                buf_filled = 0;
                buf_start_y = y + 1;
                memset(line_buf, 0, LCD_WIDTH * BUF_LINES * sizeof(uint16_t));
            }
        }
    } while (0);

    // 统一资源清理区
    if (line_buf) free_bsc(line_buf);
    if (src_buf) free_bsc(src_buf);
    if (file_opened) f_close(&fil);

    return ret;
}
