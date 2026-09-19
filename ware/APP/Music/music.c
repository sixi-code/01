#include "stm32f4xx.h"
#include "ff.h"
#include "malloc.h"
#include "fatfs.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "wav.h"
#include "mp3.h"
#include "flac.h"
#include "ape.h"
#include "aac.h"
#include "aiff.h"
#include "ogg.h"

#include "music.h"
#include "rng.h"
#include "es9018k2m.h"
#include "FreeRTOS.h"
#include "task.h"
#include "variables.h"
#include "defines.h"

// 文件类型定义(扩展名 -> FORMAT_* 编号,audio_play_task 按它分派)
#define FORMAT_WAV  1
#define FORMAT_MP3  2
#define FORMAT_FLAC 3
#define FORMAT_AAC  4
#define FORMAT_APE  5
#define FORMAT_AIFF 6
#define FORMAT_OGG  7

#define MUSIC_PATH_PREFIX "0:/MUSIC"// SD 卡上的音乐目录

#define NAME_LEN_MAX 64// 显示名最大长度(含结尾 '\0')

// 控制参数
__musicctrl music_ctrl;// 播放控制结构体(文件对象/缓冲/解码器指针)

// 音频信息(供 UI 显示)
__musicinfo music_info;// 音频信息结构体(时长/码率/采样率/位深/曲名)

// 文件管理(播放路径 + 曲目统计 + 当前索引/格式)
uint8_t *pname = NULL;// 当前播放文件的完整路径(由 audio_play_prepare 分配)
uint16_t total_files = 0;// 曲目总数(只统计受支持的后缀)
uint16_t current_index = 0;// 当前曲目索引(0 ~ total_files-1)
uint8_t current_format = 0;// 当前曲目格式(FORMAT_*,0=未知)

static char display_name_buf[NAME_LEN_MAX];// 去掉后缀的显示名缓冲(music_info.music_name 指向它)

// 不区分大小写的后缀比较
// ext: 文件后缀名(不带点)
uint8_t get_file_format(char* ext)
{
    char ext_lower[5];// 小写化后的后缀(4 字符 + '\0')
    int i;// 后缀有效长度(循环结束时的下标)
    for(i=0; i<4 && ext[i]; i++) {// 最多取 4 个字符,遇 '\0' 提前结束
        ext_lower[i] = tolower(ext[i]);
    }
    ext_lower[i] = 0;// 手动补字符串结尾

    // 逐个后缀匹配(命中的分支直接返回格式编号)
    if(strcmp(ext_lower, "wav") == 0) return FORMAT_WAV;
    if(strcmp(ext_lower, "mp3") == 0) return FORMAT_MP3;
    if(strcmp(ext_lower, "flac")== 0) return FORMAT_FLAC;
    if(strcmp(ext_lower, "aac") == 0) return FORMAT_AAC;
    if(strcmp(ext_lower, "ape") == 0) return FORMAT_APE;
    if(strcmp(ext_lower, "aif") == 0) return FORMAT_AIFF;
    if(strcmp(ext_lower, "aiff")== 0) return FORMAT_AIFF;
    if(strcmp(ext_lower, "ogg") == 0) return FORMAT_OGG;
    return 0;// 不支持的后缀
}

// 去掉文件名后缀,只保留 '.' 之前的部分(供 UI 显示曲名)
// fname: 原始文件名(带后缀)
static void update_display_name(char* fname)
{
    strncpy(display_name_buf, fname, NAME_LEN_MAX - 1);// 整串拷贝(留 1 字节给 '\0')
    display_name_buf[NAME_LEN_MAX - 1] = '\0';// strncpy 不保证补 '\0',这里强制
    char *dot = strrchr(display_name_buf, '.');// 从右往左找最后一个 '.'
    if (dot) *dot = '\0'; // 就地截断:保留 '.' 之前的部分
}

// 按索引扫描音乐目录,取出第 index_to_find 个受支持的文件
// index_to_find: 目标索引
// info_out: 输出文件信息结构体指针(可为 NULL)
// format_out: 输出文件格式指针(可为 NULL)
static uint8_t scan_music_dir(uint16_t index_to_find, FILINFO* info_out, uint8_t* format_out)
{
    DIR music_dir;// 目录对象
    FILINFO file_info;// 目录项
    FRESULT res;// FatFs 返回值
    uint16_t count = 0;// 已统计到的曲目序号
    uint8_t found = 0;// 是否命中(1=已找到)

    res = f_opendir(&music_dir, MUSIC_PATH_PREFIX);// 打开音乐目录
    if (res != FR_OK) return 0;// 目录打不开

    // 逐项遍历目录,只认受支持的后缀
    while (1) 
    {
        res = f_readdir(&music_dir, &file_info);// 读下一个目录项
        if (res != FR_OK || file_info.fname[0] == 0) break;// 读失败或已读完(文件名为空)即结束
        
        if (file_info.fattrib & (AM_DIR | AM_HID)) continue;// 跳过子目录与隐藏项

        uint8_t* ext = fatfs_get_extension(file_info.fname);// 取出扩展名
        uint8_t fmt = get_file_format((char*)ext);// 后缀 -> 格式编号(0=不支持)
        
        if (fmt != 0) 
        {
            // 命中目标索引:回填结果并结束遍历
            if (count == index_to_find) 
            {
                if (info_out) memcpy(info_out, &file_info, sizeof(FILINFO));// 调用方要目录项才回填(允许传 NULL)
                if (format_out) *format_out = fmt;// 格式编号按需回填
                found = 1;// 标记已找到
                break; 
            }
            count++;// 只统计受支持的后缀
        }
    }
    f_closedir(&music_dir);// 关闭目录
    
    return found;// 1=找到,0=未找到
}

// 按 current_index 重扫目录,刷新播放路径 / 显示名 / music_info
static void update_current_song_info(void)
{
    FILINFO temp_info;// 临时目录项(承接扫描结果)
    
    // 查找当前索引的文件信息
    if (scan_music_dir(current_index, &temp_info, &current_format))
    {
        // 更新播放路径(后续 f_open 直接用它)
        // pname 由 audio_play_prepare 分配,空间足够容纳该路径
        sprintf((char*)pname, "%s/%s", MUSIC_PATH_PREFIX, temp_info.fname);// 拼出 "0:/MUSIC/文件名"
        
        // 更新显示名(去掉后缀)
        update_display_name(temp_info.fname);// 只保留文件名主体
        
        // 填充 music_info 的静态字段
        music_info.file_format = current_format;// 格式编号
        music_info.music_name = display_name_buf;// 曲名指向静态缓冲
        music_info.file_date = temp_info.fdate;// 文件日期
        music_info.file_time = temp_info.ftime;// 文件时间
        
        // 重置动态参数(等解码器准备阶段填)
        music_info.artist_name = NULL;// 无艺术家信息(解码器解析到再填)
        music_info.total_sec = 0;// 总时长(秒)
        music_info.current_sec = 0;// 当前播放秒数
        music_info.bitrate = 0;// 码率
        music_info.samplerate = 0;// 采样率
        music_info.bit_depth = 0;// 位深
    }
    else
    {
        current_format = 0;// 索引越界(曲目被删):标记为未知格式
    }
}

// 切到下一首(到末尾回绕到第 1 首)
void play_next_song(void)
{
    if (total_files == 0) return;// 空播放列表直接返回
	current_index = (current_index == total_files-1) ? 0 : (current_index+1);// 前进一位,到末尾回绕到 0
    update_current_song_info();// 同步路径/显示名/音频信息
}

// 切到上一首(到开头回绕到末曲)
void play_previous_song(void)
{
    if (total_files == 0) return;// 空播放列表直接返回
	current_index = (current_index == 0) ? (total_files-1) : (current_index-1);// 后退一位,到开头回绕到末曲
    update_current_song_info();// 同步路径/显示名/音频信息
}

// 重播当前曲(单曲循环:只把时间归零,不动索引)
void play_same_song(void)
{
    // 如果文件被删除了，f_open 会失败，那是播放任务处理的事。
    if (total_files == 0 || pname == NULL) return;// 列表空或路径未分配
    music_info.current_sec = 0;// 时间归零:播放任务会重新准备解码器
}

// 随机切歌(尽量不与当前曲重复;只有 1 首时退化为 0)
void play_random_song(void)
{
    uint16_t old_index = current_index;// 记下当前索引,用于排除
    uint16_t new_index;// 抽到的新索引
    
    if (total_files == 0) return;// 空播放列表直接返回

    if (total_files == 1) new_index = 0;// 只有 1 首:直接选中它
	else 
	{
        do {new_index = RNG_GetRandomRange(0, total_files - 1);}// 反复抽,直到与当前曲不同
		while (new_index == old_index && total_files > 1);
    }
    current_index = new_index;// 落地新索引
    update_current_song_info();// 同步路径/显示名/音频信息
}

// 播放指定路径的歌曲(例如 "0:/MUSIC/AAA.mp3")
// filepath: 目标文件的完整路径
// 返回值: 0 表示已切换到该曲目(播放列表为空时也返回 0),1 表示未找到该文件
uint8_t play_specific_song(const char* filepath)
{
    DIR music_dir;// 目录对象
    FILINFO file_info;// 目录项
    FRESULT res;// FatFs 返回值
    uint16_t count = 0;// 已统计到的曲目序号
    uint8_t found = 0;// 是否命中(1=已找到)
    
    // 播放列表尚未建立时直接返回(调用方应先调用 audio_play_prepare)
    if (total_files == 0) return 0;// 空表

    // 从完整路径里取出文件名(去掉 "0:/MUSIC/" 前缀)
    const char *slash = strrchr(filepath, '/');// 从右往左找最后一个 '/'
    const char *fname_to_find = slash ? (slash + 1) : filepath;// '/' 之后即文件名(没有 '/' 则整串当文件名)

    // 重扫目录按文件名反查索引(UI 只给路径、不给索引)
    res = f_opendir(&music_dir, MUSIC_PATH_PREFIX);// 打开音乐目录
    if (res != FR_OK) return 0;// 目录打不开

    while (1) 
    {
        res = f_readdir(&music_dir, &file_info);// 读下一个目录项
        if (res != FR_OK || file_info.fname[0] == 0) break;// 读失败或已读完(文件名为空)即结束
        
        if (file_info.fattrib & (AM_DIR | AM_HID)) continue;// 跳过子目录与隐藏项

        uint8_t* ext = fatfs_get_extension(file_info.fname);// 取出扩展名
        uint8_t fmt = get_file_format((char*)ext);// 后缀 -> 格式编号(0=不支持)
        
        if (fmt != 0) 
        {
            // 文件名一致即认为命中(不做路径比较)
            if (strcmp(file_info.fname, fname_to_find) == 0) 
            {
                // 命中目标文件:把系统状态切到该曲目
                current_index = count;// 索引对齐到该曲目
                current_format = fmt;// 记录格式编号
                
                // 直接用 UI 传进来的完整路径
                if (pname != NULL) {
                    strcpy((char*)pname, filepath);// 拷贝路径(pname 由 prepare 分配)
                }
                
                // 更新显示名(去掉后缀)
                update_display_name(file_info.fname);// 只保留文件名主体
                
                // 填充 music_info 的静态字段
                music_info.file_format = current_format;// 格式编号
                music_info.music_name = display_name_buf;// 曲名指向静态缓冲
                music_info.file_date = file_info.fdate;// 文件日期
                music_info.file_time = file_info.ftime;// 文件时间
                
                // 重置动态参数(等解码器准备阶段填)
                music_info.artist_name = NULL;// 无艺术家信息(解码器解析到再填)
                music_info.total_sec = 0;// 总时长(秒)
                music_info.current_sec = 0;// 当前播放秒数
                music_info.bitrate = 0;// 码率
                music_info.samplerate = 0;// 采样率
                music_info.bit_depth = 0;// 位深

                found = 1;// 标记已找到
                break; 
            }
            count++;// 只统计受支持的后缀
        }
    }
    f_closedir(&music_dir);// 关闭目录
    
    return (!found);// 0=已切换(含列表为空),1=未找到
}

// 播放准备:统计音乐目录下的曲目数,并把第 1 首设为当前曲目
// 返回值: 0表示成功,1表示路径缓冲分配失败,2表示目录打不开或没有可播放文件
uint8_t audio_play_prepare(void) 
{
    DIR music_dir;// 目录对象
    FILINFO file_info;// 目录项
    FRESULT res;// FatFs 返回值
    uint32_t buf_size;// 路径缓冲需要的字节数
	
    total_files = 0;// 重新统计前清零
    current_format = 0;// 重新统计前清零
    current_index = 0;// 从第 1 首开始

    // 路径缓冲只分配一次(目录前缀 + '/' + 最长文件名 + '\0')
    if(pname == NULL)
    {
        buf_size = strlen(MUSIC_PATH_PREFIX) + 1 + FF_MAX_LFN + 1;// 目录前缀 + '/' + 最长文件名 + '\0'
        pname = malloc_bsc(buf_size);// 只分配一次(后续切歌复用)
        if(pname == NULL) return 1;// 分配失败
    }

    res = f_opendir(&music_dir, MUSIC_PATH_PREFIX);// 打开音乐目录
    if(res == FR_OK) 
	{
        // 逐项遍历目录,只认受支持的后缀
        while(1) 
		{
            res = f_readdir(&music_dir, &file_info);// 读下一个目录项
            if(res != FR_OK || file_info.fname[0] == 0) break;// 读失败或已读完(文件名为空)即结束
            
            if (file_info.fattrib & (AM_DIR | AM_HID)) continue; // 跳过子目录与隐藏项

            uint8_t* ext = fatfs_get_extension(file_info.fname);// 取出扩展名
            uint8_t fmt = get_file_format((char*)ext);// 后缀 -> 格式编号(0=不支持)
            
            if(fmt != 0) 
            {
                // 第 1 个受支持的文件 = 默认播放曲目
                if(total_files == 0)
                {
                    sprintf((char*)pname, "%s/%s", MUSIC_PATH_PREFIX, file_info.fname);// 拼出 "0:/MUSIC/文件名"
                    update_display_name(file_info.fname);// 只保留文件名主体
                    
                    current_format = fmt;// 记录格式编号
                    music_info.file_format = fmt;// 格式编号
                    music_info.music_name = display_name_buf;// 曲名指向静态缓冲
                    music_info.file_date = file_info.fdate;// 文件日期
                    music_info.file_time = file_info.ftime;// 文件时间
                    
                    // 动态参数清零(等解码器准备阶段填)
                    music_info.artist_name = NULL;// 无艺术家信息(解码器解析到再填)
                    music_info.total_sec = 0;// 总时长(秒)
                    music_info.current_sec = 0;// 当前播放秒数
                    music_info.bitrate = 0;// 码率
                    music_info.samplerate = 0;// 采样率
                    music_info.bit_depth = 0;// 位深
                }
                total_files++;// 只统计受支持的后缀
            }
        }
        f_closedir(&music_dir);// 关闭目录
    }
	
    if(total_files > 0) return 0;// 有曲目即成功
    
    audio_play_clear();// 收尾:释放路径缓冲并清状态
    return 2; // 目录打不开或没有可播放文件
}

// 播放任务分派(每轮按 current_format 把控制权交给对应格式的解码任务)
void audio_play_task(void) 
{
    if (pname == NULL) return;// 路径还没准备好就不干活

    // 按扩展名识别出的格式分派(各格式自带 Music_Status 状态机)
    switch(current_format) 
	{
        case FORMAT_WAV:
            wav_play_song_task(pname);
            break;
        case FORMAT_MP3:
            mp3_play_song_task(pname);
            break;
        case FORMAT_FLAC:
			flac_play_song_task(pname);
            break;
        case FORMAT_AAC:
			aac_play_song_task(pname);
            break;
		case FORMAT_APE:
			ape_play_song_task(pname);
            break;
		case FORMAT_AIFF:
			aiff_play_song_task(pname);
			break;
		case FORMAT_OGG:
			ogg_play_song_task(pname);
			break;
        default:
            break;
    }
}

// 播放收尾(释放路径缓冲并清空播放状态;没有曲目时也会走到这里)
void audio_play_clear(void) 
{
	if(pname)// 路径缓冲可能还没分配
	{
        free_bsc(pname);// 归还堆
        pname = NULL;// 指针置空防野指针
    }
    music_info.music_name = NULL;// 显示名缓冲是静态数组,只解引用
    total_files = 0;// 曲目总数清零
    current_index = 0;// 索引归零
    current_format = 0;// 清空格式
}

// 统一的跳转接口(暴露给 UI 拖动进度条使用)
// target_sec: 目标秒数
void audio_seek(uint32_t target_sec)
{
    // 未打开文件或总时长未知时拒绝操作
    if (music_ctrl.file == NULL || music_info.total_sec == 0) return;// 保护:无文件或时长未知
    // 越界夹紧:最多跳到 总时长 - 1 秒
    if (target_sec >= music_info.total_sec) {
        target_sec = music_info.total_sec - 1;
    }
    // seek 四流派:字节比例(WAV/AIFF)、字节比例+重同步(MP3/AAC/FLAC)、帧索引查表(APE)、解码器原生(OGG)
    switch(music_info.file_format) 
    {
        case FORMAT_WAV:// WAV:原始 PCM,按字节率直接换算文件偏移
            if (wavctrl.bitrate > 0) {// 码率未知时不跳
                uint32_t target_pos = wavctrl.datastart + target_sec * (wavctrl.bitrate / 8);// 偏移 = 数据起点 + 秒数 x 每秒字节数
                wav_file_seek(target_pos);
            }
            break;
            
        case FORMAT_MP3:// MP3/AAC/FLAC:按字节比例定位,再让解码器重新同步
            if (mp3ctrl && mp3ctrl->totsec > 0) {// 上下文与总时长都有效才跳
                uint32_t data_size = f_size(music_ctrl.file) - mp3ctrl->datastart;// 音频数据总字节数
                uint32_t target_pos = mp3ctrl->datastart + (uint32_t)(((uint64_t)target_sec * data_size) / mp3ctrl->totsec);// 按时间比例换算(64 位乘法防溢出)
                mp3_file_seek(target_pos);
            }
            break;
            
        case FORMAT_FLAC:
            if (flacctrl && flacctrl->totsec > 0) {// 上下文与总时长都有效才跳
                uint32_t data_size = f_size(music_ctrl.file) - flacctrl->datastart;// 音频数据总字节数
                uint32_t target_pos = flacctrl->datastart + (uint32_t)(((uint64_t)target_sec * data_size) / flacctrl->totsec);// 按时间比例换算(64 位乘法防溢出)
                flac_file_seek(target_pos);
            }
            break;
            
        case FORMAT_AAC:
            if (aacctrl && aacctrl->totsec > 0) {// 上下文与总时长都有效才跳
                uint32_t data_size = f_size(music_ctrl.file) - aacctrl->datastart;// 音频数据总字节数
                uint32_t target_pos = aacctrl->datastart + (uint32_t)(((uint64_t)target_sec * data_size) / aacctrl->totsec);// 按时间比例换算(64 位乘法防溢出)
                aac_file_seek(target_pos);
            }
            break;
            
        case FORMAT_APE:
            // APE 是帧对齐，直接传目标秒数让底层查表
            ape_file_seek(target_sec);
            break;

        case FORMAT_AIFF:
            // AIFF 是原始 PCM,同 WAV 按字节偏移计算
            if (aiffctrl.bitrate > 0) {// 码率未知时不跳
                uint32_t target_pos = aiffctrl.datastart + target_sec * (aiffctrl.bitrate / 8);// 偏移 = 数据起点 + 秒数 x 每秒字节数
                aiff_file_seek(target_pos);
            }
            break;
        case FORMAT_OGG:// OGG:解码器自带帧索引,走原生 seek
            ogg_file_seek(target_sec);
            break;
        default:
            break;
    }
    
    // 同步显示时间,防止 UI 在拖动后短暂回弹
    music_info.current_sec = target_sec;// 立即同步给 UI
}
