#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "ff.h"
#include "tjpgd.h"
#include "lcd_bsp.h"
#include "malloc.h"
#include <string.h>
#include "lvgl.h"
#include "variables.h"
#include "defines.h"
#include "debug.h"
#include "avi.h"
#include "i2s.h"

// --- 视频配置 ---
#define LCD_WIDTH  240
#define LCD_HEIGHT 240
#define BUF_LINES  16        // 极限对齐 MCU 尺寸，双重缓冲仅需 7.5KB
#define WORKBUF_SIZE 10240   // TJpgDec 工作区大小：给足10KB，绝对防止解不开复杂帧头！

// --- 音频配置 ---
#define AVI_AUDIO_BUF_SIZE   8192   // I2S DMA 硬件双缓冲总大小 (Ping-Pong各4KB)
#define AVI_AUDIO_FIFO_SIZE  16384  // 软件环形缓冲区 (提供约 90ms 弹性抗抖动能力)

// 宏定义：将四个字符组合为 32-bit 的 chunk ID
// 用于识别 AVI 文件中的不同块类型，例如 'RIFF', 'AVI ', 'LIST', 'movi' 等
#define FOURCC(a,b,c,d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

static FIL fil;// AVI 文件句柄
static uint8_t *workbuf = NULL;// TJpgDec 工作缓冲区     
static int file_opened = 0;// 文件是否已打开
static int initialized = 0;// 是否已初始化
static uint32_t movi_offset = 0;// movi 块在文件中的偏移量

// --- 视频相关状态 ---
static uint16_t *line_buf[2] = {NULL, NULL};// 视频双缓冲区 
static uint8_t write_idx = 0;// 当前 CPU 正在写入的缓冲区索引 (0 或 1)
static uint8_t dma_busy = 0;// 标记 DMA 是否正在传输
static uint16_t buf_start_y = 0;// 缓冲区当前起始行
static uint16_t buf_filled_lines = 0;// 缓冲区有效行数

// --- 音频相关状态 ---
static uint8_t has_audio = 0;// 标记 AVI 文件是否包含音频流
static uint8_t dma_started = 0;// 标记音频 DMA 是否已启动
static uint16_t audio_channels = 2;// 音频通道数 (1=单声道, 2=立体声)
static uint32_t audio_samplerate = 44100;// 音频采样率
static uint16_t audio_bps = 16;// 音频位深
static uint8_t *audio_buf[2] = {NULL, NULL};// I2S DMA 双缓冲区

// --- 高性能环形缓冲 ---
static uint8_t *audio_fifo = NULL;// 软件环形缓冲区
static volatile uint32_t fifo_wr = 0;// 写指针
static volatile uint32_t fifo_rd = 0;// 读指针
static volatile uint32_t fifo_count = 0;// 缓冲区内有效数据字节数

// --------------------------- 音频核心驱动区 ---------------------------

// 【零拷贝提取】从环形缓冲区提取数据喂给 DMA
// out_buf: DMA 输出缓冲区指针
// len: 要提取的字节数 (必须为偶数，且不超过 AVI_AUDIO_BUF_SIZE / 2)
// 如果环形缓冲区数据不足，则输出静音防爆音
static void pull_audio_fifo(uint8_t *out_buf, uint32_t len) {
    if (fifo_count < len) {
        memset(out_buf, 0, len); // 缓冲不足则输出静音防爆音
        return;
    }

    uint32_t right_part = AVI_AUDIO_FIFO_SIZE - fifo_rd;// 计算从读指针到缓冲区末尾的连续可读字节数
    // 如果要提取的字节数小于等于连续可读字节数，则直接 memcpy
    if (len <= right_part) {
        memcpy(out_buf, &audio_fifo[fifo_rd], len);
        fifo_rd += len;
        if (fifo_rd == AVI_AUDIO_FIFO_SIZE) fifo_rd = 0;
    }
    // 否则需要分两次 memcpy，先拷贝右侧部分，再拷贝左侧部分 
    else {
        memcpy(out_buf, &audio_fifo[fifo_rd], right_part);
        memcpy(out_buf + right_part, audio_fifo, len - right_part);
        fifo_rd = len - right_part;
    }
    // 更新环形缓冲区内有效数据字节数
    vPortEnterCritical();// 进入临界区，防止中断修改 fifo_count
    fifo_count -= len;
    vPortExitCritical();
}

// 供各种碎片时间高频轮询的无阻塞音频服务
static void audio_service(void) {
    if (!has_audio || !dma_started) return;
    if (xSemaphoreTake(xI2SSemaphore, 0) == pdTRUE) {
        pull_audio_fifo(audio_buf[I2SdmaBuff], AVI_AUDIO_BUF_SIZE / 2);
    }
}

// --------------------------- 视频双缓冲与解码回调 ---------------------------


// 等待视频 DMA 传输完成，期间持续调用 audio_service() 以防止音频 DMA 饿死
static void wait_video_dma_done(void) {
    while (dma_busy) {
        // 使用短暂超时确保在等待屏幕刷新时绝不饿死音频 DMA
        if (xEventGroupWaitBits(xLcdEventGroup, LCD_USER_MDIA, pdTRUE, pdFALSE, pdMS_TO_TICKS(2)) != 0) {
            dma_busy = 0;
            break;
        }
        audio_service(); 
    }
}

// TJpgDec 输入回调函数：从 AVI 文件中读取 JPEG 数据
// jd: JPEG 解码器对象
// buff: 数据缓冲区指针 (如果为 NULL，则表示跳过 nbyte 字节)
// nbyte: 要读取或跳过的字节数
// 返回值：实际读取的字节数 (如果为 0，则表示读取失败或 EOF)
static size_t in_func(JDEC *jd, uint8_t *buff, size_t nbyte) {
    audio_service(); 
    FIL *fp = (FIL*)jd->device;
    UINT br;
    if (buff) {
        if (f_read(fp, buff, nbyte, &br) != FR_OK) return 0;
        return br;
    } else {
        if (f_lseek(fp, f_tell(fp) + nbyte) != FR_OK) return 0;
        return nbyte;
    }
}


// 将缓冲区内容刷新到 LCD 上，并更新缓冲区状态
static void flush_line_buffer(void) {
    if (buf_filled_lines == 0) return;
    wait_video_dma_done();// 等待上一次 DMA 传输完成(并在此期间服务音频 DMA)
    LCD_Color_Fill(0, buf_start_y, LCD_WIDTH - 1, buf_start_y + buf_filled_lines - 1, line_buf[write_idx]);
    dma_busy = 1;
    buf_start_y += buf_filled_lines;// 更新缓冲区起始行
    buf_filled_lines = 0;// 清空缓冲区有效行数
    write_idx ^= 1;
}

// TJpgDec 输出回调函数：处理解码后的 JPEG 图像数据
// jd: JPEG 解码器对象
// bitmap: 解码后的图像数据缓冲区指针 (RGB565 格式
// rect: 解码后的图像区域 (左、右、上、下)
// 返回值：1 表示继续解码，0 表示停止解码
static int out_func(JDEC *jd, void *bitmap, JRECT *rect) {
    audio_service(); 
    uint16_t *src = (uint16_t*)bitmap;
    uint16_t left = rect->left, right = rect->right, top = rect->top, bottom = rect->bottom;

    for (uint16_t y = top; y <= bottom; y++) {
        while (y >= buf_start_y + BUF_LINES) flush_line_buffer();// 如果当前行超出缓冲区范围，则先刷新缓冲区
        int row_in_buf = y - buf_start_y;// 计算当前行在缓冲区中的索引
        
        // 只拷贝在 LCD 显示范围内的像素数据，避免越界访问
        if (row_in_buf >= 0 && left < LCD_WIDTH) {
            uint16_t copy_right = (right >= LCD_WIDTH) ? (LCD_WIDTH - 1) : right;
            uint16_t width = copy_right - left + 1;
            uint16_t *dst = &line_buf[write_idx][row_in_buf * LCD_WIDTH + left];
            memcpy(dst, src, width * sizeof(uint16_t));
            
            // 更新缓冲区有效行数，确保不会超过 BUF_LINES
            if (row_in_buf + 1 > buf_filled_lines) buf_filled_lines = row_in_buf + 1;
        }
        // 更新源数据指针，跳过当前行的像素数据
        src += (right - left + 1); 
    }
    return 1;
}

// --------------------------- 核心控制流程 ---------------------------

// 初始化 AVI 播放器，解析 AVI 文件头，准备视频和音频播放
// file: AVI 文件路径
// 返回值：0 表示成功，1 表示失败
uint8_t video_avi_play_init(const char *file)
{
    uint32_t header[3];// RIFF AVI 文件头
    UINT br;// 实际读取的字节数
    uint32_t current_stream = 0;// 当前正在解析的流类型 (0=未知, 1=视频, 2=音频) 
    if (initialized) return 0;
    // 重置状态
    has_audio = 0; 
    dma_started = 0;
    fifo_wr = 0; 
    fifo_rd = 0;
    fifo_count = 0;
    movi_offset = 0;
    
    if (f_open(&fil, file, FA_READ) != FR_OK) return 1;
    file_opened = 1;

    if (f_read(&fil, header, 12, &br) != FR_OK || br != 12) goto err;
    if (header[0] != FOURCC('R','I','F','F') || header[2] != FOURCC('A','V','I',' ')) goto err;

    while (1) {
        uint32_t chunk[2];// 读取下一个 chunk 的 ID 和大小
        if (f_read(&fil, chunk, 8, &br) != FR_OK || br != 8) break;
        uint32_t cid = chunk[0], csize = chunk[1];// 读取 chunk ID 和大小
        uint32_t align_size = (csize + 1) & ~1;// 对齐到偶数字节边界

        // 处理不同类型的 chunk
        if (cid == FOURCC('L','I','S','T')) {
            uint32_t list_type;// 读取 LIST chunk 的类型
            // 读取 LIST chunk 的类型
            if (f_read(&fil, &list_type, 4, &br) != FR_OK) break;
            // 如果是 movi 块，则记录其偏移量并退出循环
            if (list_type == FOURCC('m','o','v','i')) {
                movi_offset = f_tell(&fil); 
                break; 
            }
            // 如果是 strl 块，则继续解析其子块，否则跳过该 LIST 块
            if (list_type == FOURCC('h','d','r','l') || list_type == FOURCC('s','t','r','l')) {
                continue; // 允许游标自然进入该 List 内部读取子组件
            }
            f_lseek(&fil, f_tell(&fil) + align_size - 4);
            continue; 
        }
        // 处理 strh 块，确定当前流类型 (视频或音频)
        else if (cid == FOURCC('s','t','r','h')) {
            // 处理流头块
            uint32_t fccType;
            if (f_read(&fil, &fccType, 4, &br) == FR_OK) {
                // 根据 fccType 判断当前流类型
                if (fccType == FOURCC('v','i','d','s')) current_stream = 1;
                // 处理音频流
                else if (fccType == FOURCC('a','u','d','s')) current_stream = 2;
                f_lseek(&fil, f_tell(&fil) + align_size - 4);
            }
        }
        // 处理 strf 块，读取音频流的格式信息
        else if (cid == FOURCC('s','t','r','f')) {
            if (current_stream == 2) {
                uint16_t wav_fmt[8]; 
                // 读取音频流的格式信息 (WAVEFORMATEX)
                if (f_read(&fil, wav_fmt, 16, &br) == FR_OK) {
                    audio_channels = wav_fmt[1];
                    audio_samplerate = wav_fmt[2] | ((uint32_t)wav_fmt[3] << 16);
                    audio_bps = wav_fmt[7];
                    has_audio = 1;
                    f_lseek(&fil, f_tell(&fil) + align_size - 16);
                }
            }
            // 如果当前流不是音频流，则跳过该 strf 块 
            else f_lseek(&fil, f_tell(&fil) + align_size);
        }
        // 如果当前流不是视频流或音频流，则跳过该 chunk
        else f_lseek(&fil, f_tell(&fil) + align_size);
    }
    
    if (movi_offset == 0) goto err;
    
    // 分配工作缓冲区和行缓冲区
    workbuf = (uint8_t*)malloc_bsc(WORKBUF_SIZE);
    line_buf[0] = (uint16_t*)malloc_bsc(LCD_WIDTH * BUF_LINES * sizeof(uint16_t));
    line_buf[1] = (uint16_t*)malloc_bsc(LCD_WIDTH * BUF_LINES * sizeof(uint16_t));
    if (!workbuf || !line_buf[0] || !line_buf[1]) goto err;

    // 如果 AVI 文件包含音频流，则分配音频缓冲区和环形缓冲区，并初始化 I2S 音频输出
    if (has_audio) {
        audio_buf[0] = (uint8_t*)malloc_bsc(AVI_AUDIO_BUF_SIZE / 2);
        audio_buf[1] = (uint8_t*)malloc_bsc(AVI_AUDIO_BUF_SIZE / 2);
        audio_fifo   = (uint8_t*)malloc_bsc(AVI_AUDIO_FIFO_SIZE);
        if (!audio_buf[0] || !audio_buf[1] || !audio_fifo) goto err;

        memset(audio_buf[0], 0, AVI_AUDIO_BUF_SIZE / 2);
        memset(audio_buf[1], 0, AVI_AUDIO_BUF_SIZE / 2);

        I2S2_Init(I2S_Standard_Phillips, I2S_Mode_MasterTx, I2S_CPOL_Low, I2S_DataFormat_16b);
        I2S2_SampleRate_Set(audio_samplerate);
    
        I2S2_TX_DMA_Init(audio_buf[0], audio_buf[1], (AVI_AUDIO_BUF_SIZE / 2) / 2); 
        
        if (xI2SSemaphore != NULL) xSemaphoreTake(xI2SSemaphore, 0); 
    }
    // 初始化缓冲区状态
    buf_start_y = 0; buf_filled_lines = 0; write_idx = 0; dma_busy = 0;
    initialized = 1;
    return 0;

err:
    video_avi_play_deinit();
    return 1;
}


// 解析 AVI 文件并播放视频和音频
// 返回值：0 表示成功解析一帧画面，1 表示未初始化，2 表示播放结束或出错
uint8_t video_avi_play_task(void) {
    if (!initialized) return 1;

    while (1) {
        audio_service(); 
        
        uint32_t chunk[2]; UINT br;
        if (f_read(&fil, chunk, 8, &br) != FR_OK || br != 8) {
            if (movi_offset != 0) {
                f_lseek(&fil, movi_offset);
                vPortEnterCritical();
                fifo_wr = 0; fifo_rd = 0; fifo_count = 0; // 清空历史残音
                vPortExitCritical();
                continue; 
            }
            return 2; 
        }
        
        uint32_t cid = chunk[0], csize = chunk[1];
        uint32_t align_size = (csize + 1) & ~1;
        uint8_t *id = (uint8_t*)&cid;
        uint32_t chunk_start_pos = f_tell(&fil);
        // 处理 LIST 块，检查其类型是否为 movi 或 rec，如果是则继续解析其子块，否则跳过该 LIST 块
        if (cid == FOURCC('L','I','S','T')) {
            uint32_t list_type;
            // 读取 LIST 块的类型
            if (f_read(&fil, &list_type, 4, &br) == FR_OK) {
                // 如果 LIST 块的类型不是 movi 或 rec，则跳过该 LIST 块
                if (list_type == FOURCC('m','o','v','i') || list_type == FOURCC('r','e','c',' ')) continue;
            }
            // 如果 LIST 块的类型不是 movi 或 rec，则跳过该 LIST 块
            f_lseek(&fil, chunk_start_pos + align_size);
            continue;
        }

        // 处理不同类型的 chunk

        // 如果 chunk ID 的后两个字符是 "dc"，则表示这是一个视频帧数据块，使用 JPEG 解码器解码并显示该帧
        if (id[2] == 'd' && id[3] == 'c') {
            JDEC jdec;
            jdec.pool = workbuf; jdec.sz_pool = WORKBUF_SIZE;
            // 将 AVI 文件句柄传递给 JPEG 解码器对象，以便在输入回调函数中读取 JPEG 数据
            if (jd_prepare(&jdec, in_func, workbuf, WORKBUF_SIZE, &fil) == JDR_OK) {
                buf_start_y = 0; buf_filled_lines = 0;
                jd_decomp(&jdec, out_func, 0);
                if (buf_filled_lines > 0) flush_line_buffer();// 刷新剩余的缓冲区内容到 LCD 上
                wait_video_dma_done();// 等待最后一帧画面 DMA 传输完成(并在此期间服务音频 DMA)
            }
            
            f_lseek(&fil, chunk_start_pos + align_size);
            return 0; // 成功解析一帧画面，让出控制权响应按键
        }
        // 如果 chunk ID 的后两个字符是 "wb"，则表示这是一个音频数据块，读取并处理音频数据
        else if (id[2] == 'w' && id[3] == 'b') {
            if (has_audio) {
                uint32_t left = csize;// 剩余要读取的音频数据字节数
                // 循环读取音频数据块，直到读取完毕或缓冲区满
                while (left > 0) {
                    uint32_t free_space = AVI_AUDIO_FIFO_SIZE - fifo_count;
                    // 如果是单声道16位音频，则需要将每个采样值复制两次以实现立体声输出
                    if (audio_channels == 1 && audio_bps == 16) {
                        uint8_t temp_buf[256];// 临时缓冲区，用于读取音频数据块的一部分
                        uint32_t read_len = (left > sizeof(temp_buf)) ? sizeof(temp_buf) : left;// 计算本次读取的字节数，不能超过临时缓冲区大小
                        
                        // 如果环形缓冲区剩余空间不足以容纳本次读取的数据，则等待 DMA 传输完成并提取数据到 I2S DMA 缓冲区
                        if (free_space < read_len * 2) {
                            if (dma_started) {
                                // 等待音频 DMA 传输完成，并从环形缓冲区提取数据到 I2S DMA 缓冲区
                                if (xSemaphoreTake(xI2SSemaphore, pdMS_TO_TICKS(50)) == pdTRUE) {
                                    pull_audio_fifo(audio_buf[I2SdmaBuff], AVI_AUDIO_BUF_SIZE / 2);
                                } else left = 0; 
                            } else break; 
                            continue;
                        }
                        
                        // 从 AVI 文件中读取音频数据块的一部分到临时缓冲区，并将其转换为立体声输出到环形缓冲区
                        if (f_read(&fil, temp_buf, read_len, &br) == FR_OK && br > 0) {
                            int16_t *src = (int16_t*)temp_buf;
                            for (uint32_t i = 0; i < br / 2; i++) {
                                int16_t val = src[i];
                                audio_fifo[fifo_wr++] = val & 0xFF; if(fifo_wr == AVI_AUDIO_FIFO_SIZE) fifo_wr = 0;
                                audio_fifo[fifo_wr++] = (val >> 8) & 0xFF; if(fifo_wr == AVI_AUDIO_FIFO_SIZE) fifo_wr = 0;
                                audio_fifo[fifo_wr++] = val & 0xFF; if(fifo_wr == AVI_AUDIO_FIFO_SIZE) fifo_wr = 0;
                                audio_fifo[fifo_wr++] = (val >> 8) & 0xFF; if(fifo_wr == AVI_AUDIO_FIFO_SIZE) fifo_wr = 0;
                            }
                            vPortEnterCritical(); fifo_count += (br * 2); vPortExitCritical();
                            left -= br;
                        } else break;
                    } 
                    else {
                        // 处理环形缓冲区的剩余空间
                        uint32_t right_part = AVI_AUDIO_FIFO_SIZE - fifo_wr;
                        uint32_t read_len = (left > right_part) ? right_part : left;
                        if (read_len > free_space) read_len = free_space;
                        // 如果剩余空间不足以容纳本次读取的数据，则等待 DMA 传输完成并提取数据到 I2S DMA 缓冲区
                        if (read_len > 0) {
                            if (f_read(&fil, &audio_fifo[fifo_wr], read_len, &br) == FR_OK && br > 0) {
                                fifo_wr += br;
                                if (fifo_wr == AVI_AUDIO_FIFO_SIZE) fifo_wr = 0;
                                vPortEnterCritical(); fifo_count += br; vPortExitCritical();
                                left -= br;
                            } else break;
                        } else {
                            if (dma_started) {
                                if (xSemaphoreTake(xI2SSemaphore, pdMS_TO_TICKS(50)) == pdTRUE) {
                                    pull_audio_fifo(audio_buf[I2SdmaBuff], AVI_AUDIO_BUF_SIZE / 2);
                                } else break; 
                            } else break;
                        }
                    }
                    
                    // 等到 FIFO 攒够一半水量（避免空响）再让 DMA 上班发声
                    if (!dma_started && fifo_count >= (AVI_AUDIO_FIFO_SIZE / 2)) {
                        if (xI2SSemaphore != NULL) xSemaphoreTake(xI2SSemaphore, 0); 
                        I2S_Play_Start();
                        dma_started = 1;
                    }
                }
            }
            f_lseek(&fil, chunk_start_pos + align_size);
        }
        // 如果 chunk ID 是 'idx1'，则表示这是一个索引块，直接跳转到 movi 块的起始位置，并清空环形缓冲区
        else if (cid == FOURCC('i','d','x','1')) {
            f_lseek(&fil, movi_offset);
            vPortEnterCritical();
            fifo_wr = 0; fifo_rd = 0; fifo_count = 0;
            vPortExitCritical();
        }
        else {
            f_lseek(&fil, chunk_start_pos + align_size);
        }
    }
}

// 释放 AVI 播放器资源，停止视频和音频播放
void video_avi_play_deinit(void) 
{
    if (!initialized && !file_opened) return;

    wait_video_dma_done();

    if (has_audio) {
        I2S_Play_Stop();
        if (audio_buf[0]) { free_bsc(audio_buf[0]); audio_buf[0] = NULL; }
        if (audio_buf[1]) { free_bsc(audio_buf[1]); audio_buf[1] = NULL; }
        if (audio_fifo)   { free_bsc(audio_fifo);   audio_fifo = NULL;   }
        has_audio = 0;
    }

    if (line_buf[0]) { free_bsc(line_buf[0]); line_buf[0] = NULL; }
    if (line_buf[1]) { free_bsc(line_buf[1]); line_buf[1] = NULL; }
    if (workbuf) { free_bsc(workbuf); workbuf = NULL; }
    if (file_opened) { f_close(&fil); file_opened = 0; }
    
    initialized = 0;
    dma_started = 0;
}
