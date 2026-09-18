#include "stm32f4xx.h"                  // Device header
#include "flac.h"
#include "i2s.h"
#include "malloc.h"
#include "systick_conf.h"
#include "music.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "math.h"
#include "variables.h"
#include "defines.h"
#include "spectrum_dsp.h"
#include "file_unit.h"

__flacctrl * flacctrl;	// FLAC解码控制结构体(全局指针)

// FLAC 文件识别与头部解析(用 foxen-flac 流式解码器逐块消费 metadata)
// fx: 文件对象指针
// fctrl: FLAC控制结构体指针
// 返回值: 0表示成功,1表示解析缓冲分配失败,3表示解码器创建失败,
// 4表示解码出错,5表示未解析到有效元数据(文件非法或头部过长)
uint8_t flac_init(FIL* fx, __flacctrl* fctrl)
{
    uint8_t res = 0;// 返回值,0表示成功,其他表示失败
    uint8_t *buf;// 元数据解析缓冲(1KB)
    uint32_t br;// 实际读取的字节数
    
    // 分配元数据解析缓冲(临时,用完即释放)
    buf = malloc_bsc(1024);
    if(!buf) return 1;

    // 回到文件头,从 magic 开始解析
    f_lseek(fx, 0);
    
    // 创建 foxen-flac 解码器(内存取自预置的分配回调)
    fctrl->decoder = FX_FLAC_ALLOC_SUBSET_FORMAT_DAT();
    if(!fctrl->decoder) 
	{
        free_bsc(buf);
        return 3;
    }
    
    // 读取并处理元数据
    uint32_t total_read = 0;// 累计已从文件读入的字节数
    uint8_t metadata_done = 0;// 元数据是否解析完成(1=已进入音频帧)
    uint32_t in_buf_wr_cur = 0;// 缓冲中尚未被解码器消费的字节数
	
    while(!metadata_done && total_read < 65536) 
	{
        uint32_t to_read = 1024 - in_buf_wr_cur;  // 只读剩余空间
        f_read(fx, buf + in_buf_wr_cur, to_read, &br);
        if(br == 0) break;
        
        uint32_t in_len = in_buf_wr_cur + br;// 送进解码器的长度(出参:实际消费的长度)
        
        fx_flac_state_t state = fx_flac_process(fctrl->decoder, buf, &in_len, NULL, NULL);
        
        if(state > FLAC_END_OF_METADATA) metadata_done = 1;// 已越过元数据区,流信息就绪
		if(state == FLAC_ERR) {res = 4;break;}

        uint32_t remaining = (in_buf_wr_cur + br) - in_len;// 未被消费的残留字节,挪到缓冲区头部
        memmove(buf, buf + in_len, remaining);
        in_buf_wr_cur = remaining;
        
        total_read += br;// 累计读取量(头部异常时靠 65536 上限兜底)
    }
    
    // 元数据解析完成:取出流信息并算出总时长
    if(metadata_done) 
	{
        // 获取流信息
        fctrl->samplerate = fx_flac_get_streaminfo(fctrl->decoder, FLAC_KEY_SAMPLE_RATE);// 采样率
        fctrl->nchannels = fx_flac_get_streaminfo(fctrl->decoder, FLAC_KEY_N_CHANNELS);// 声道数
        fctrl->bps = fx_flac_get_streaminfo(fctrl->decoder, FLAC_KEY_SAMPLE_SIZE);// 位深
        uint64_t total_samples = fx_flac_get_streaminfo(fctrl->decoder, FLAC_KEY_N_SAMPLES);// 总采样点数(FLAC 总时长可精确得到)
        uint32_t max_block_size = fx_flac_get_streaminfo(fctrl->decoder, FLAC_KEY_MAX_BLOCK_SIZE);// 最大块长度
        
        fctrl->totsec = total_samples / fctrl->samplerate;// 总秒数 = 总采样点数 / 采样率
        fctrl->datastart = fx->fptr - in_buf_wr_cur;// 音频数据起始位置 = 当前文件指针 - 残留字节
        
        // 平均码率 = 音频数据字节数 * 8 / 总秒数
        uint32_t file_size = f_size(fx);// 文件总大小
        fctrl->bitrate = ((file_size - fctrl->datastart) * 8) / fctrl->totsec;
		
		// 定位到音频数据起始处,后续只读音频帧
		f_lseek(fx,fctrl->datastart);
    } 
	else res = 5;
	
    free_bsc(buf);
    return res;
}

#define IN_BUF_SIZE 8*1024// 压缩数据输入缓冲 8KB
#define OUT_BUF_SIZE 16*1024// PCM 输出缓冲 16KB

// FLAC 解码状态(seek 与播放任务共享)
uint8_t* flac_in_buffer=0;// 压缩数据输入缓冲区(文件读取与解码器共享)

uint32_t in_buf_byte_left;// 输入缓冲中剩余可解码字节数
uint32_t in_buf_offset = 0;// 输入缓冲读游标(已消费到的位置)
uint32_t out_buf_byte_left;// 本次还需填充的输出字节数
uint32_t br=0;// 实际读取的字节数
  
uint32_t flac_fptr=0;// 上次记录的文件指针(用于检测 seek 引起的跳变)

// 得到当前播放时间(按已读字节比例换算,FLAC 帧长可变故不做帧对齐)
// fx: 文件对象指针
// flacctrl: FLAC控制结构体指针
void flac_get_curtime(FIL* fx, __flacctrl *flacctrl)
{
    if(fx->fptr > flacctrl->datastart) 
	{
        uint64_t fpos = fx->fptr - flacctrl->datastart;// 已播放的音频数据字节数
        uint64_t total_size = f_size(fx) - flacctrl->datastart;// 音频数据总字节数
        flacctrl->cursec = (fpos * flacctrl->totsec) / total_size;// 当前秒数 = 已读字节 / 总字节 * 总秒数
    } 
	else 
        flacctrl->cursec = 0;// 文件指针还在头部时视为 0
}

// FLAC 文件快进快退函数(清空解码器位流缓存,强制 Task 内重新读文件)
// pos: 需要定位到的文件绝对位置
// 返回值: 实际定位到的文件位置
uint32_t flac_file_seek(uint32_t pos)
{
    uint32_t file_size = f_size(music_ctrl.file);// 文件总大小
    // 把目标位置夹紧到 [音频数据起点, 文件尾] 区间
    if(pos > file_size) pos = file_size;
    if(pos < flacctrl->datastart) pos = flacctrl->datastart;

    f_lseek(music_ctrl.file, pos);

    // 强制清空 foxen-flac 内部的 bitstream 缓存
    fx_flac_flush(flacctrl->decoder);

    in_buf_byte_left = 0;// 丢弃输入缓冲里的残留压缩数据
    in_buf_offset = 0;    // 强制复位缓冲游标
    // 强制触发 Task 内的文件全量重读机制
    flac_fptr = 0xFFFFFFFF;
    
    return music_ctrl.file->fptr;// 返回实际定位到的位置
}

// 播放某个 FLAC 文件准备阶段(解析头部 + 配置 I2S + 打开文件 + 预读首块压缩数据)
// fname: 文件名
// 返回值: 0表示成功,1表示内存申请失败,2表示缓冲区分配失败,
// 其他为 f_open / flac_init 的错误码
uint8_t flac_play_song_prepare(uint8_t* fname)
{ 
    uint8_t res = 0;// 返回值,0表示成功,其他表示失败
    
    // 内存分配(控制结构 + 文件对象 + 双 DMA 缓冲 + 压缩数据缓冲)
    flacctrl = malloc_bsc(sizeof(__flacctrl));// FLAC控制结构体
    music_ctrl.file = (FIL*)malloc_bsc(sizeof(FIL));// 文件对象
    
    if(!music_ctrl.file || !flacctrl) res = 1; // 内存申请错误

	if(!res) {
        memset(flacctrl, 0, sizeof(__flacctrl));// 结构体清零
        // 打开文件
        res = f_open(music_ctrl.file, (char*)fname, FA_READ);
	}
	
	if(!res) {
		res = flac_init(music_ctrl.file, flacctrl);// 解析文件与头信息
	}
	
	if(!res) {
		// 缓冲区分配(输出 PCM 双缓冲 + 输入压缩数据缓冲)
		music_ctrl.i2sbuf1 = malloc_bsc(OUT_BUF_SIZE);// I2S DMA 缓冲区1(ping)
		music_ctrl.i2sbuf2 = malloc_bsc(OUT_BUF_SIZE);// I2S DMA 缓冲区2(pong)
		flac_in_buffer = malloc_bsc(IN_BUF_SIZE);// 压缩数据输入缓冲区
		
		// 配置 I2S(FLAC 统一按 32 位帧发送,单声道时在半字内复制展开)
		I2S2_Init(I2S_Standard_Phillips, I2S_Mode_MasterTx, I2S_CPOL_Low, I2S_DataFormat_32b);

        music_info.total_sec = flacctrl->totsec;    // 总秒数
        music_info.bitrate = flacctrl->bitrate;     // 比特率
        music_info.samplerate = flacctrl->samplerate; // 采样率
        music_info.bit_depth = flacctrl->bps;       // 位深
        music_info.current_sec = 0;                 // 当前秒数清零

		// 缓冲区全部申请成功才继续配置硬件
		if(music_ctrl.i2sbuf1 && music_ctrl.i2sbuf2 && flac_in_buffer) 
		{
			// 初始化缓冲区
			memset(music_ctrl.i2sbuf1, 0, OUT_BUF_SIZE);
			memset(music_ctrl.i2sbuf2, 0, OUT_BUF_SIZE);
			
			// 配置DMA和I2S
			I2S2_TX_DMA_Init(music_ctrl.i2sbuf1, music_ctrl.i2sbuf2, OUT_BUF_SIZE/2);// dma是2byte(16bit)
			
			I2S2_SampleRate_Set(flacctrl->samplerate);// 设置采样率

			// 读取初始数据到输入缓冲区
			f_read(music_ctrl.file, flac_in_buffer, IN_BUF_SIZE, &br);
			in_buf_byte_left = br;// 记录缓冲中可解码的字节数
            in_buf_offset = 0;  // 复位缓冲游标
			flac_fptr = music_ctrl.file->fptr;// 记录当前文件指针
			
			I2S_Play_Start();// 启动播放
		} 
		else res = 2; // 缓冲区分配失败
    }
    return res;
}

// 播放任务逻辑(由 music.c 的 audio_play_task 按 current_format 分派调用)
// fname: 文件名
void flac_play_song_task(uint8_t* fname)
{
    uint8_t* target_buf = NULL;// 本次要填充的目标缓冲区
    
    // 阶段1: 准备(解析头部并进入播放态)
    if(Music_Status == Song_Prepare) 
	{
        uint8_t res = flac_play_song_prepare(fname);// 解析头部 + 预读首块
        if(res) Music_Status = Song_End;
        else Music_Status = Song_Playing;
    }
    // 阶段2: 播放中(DMA 传输完成中断驱动,边解码边填 ping-pong 缓冲)
    else if(Music_Status == Song_Playing) 
	{
        // 等待 DMA 传输完成信号量
        xSemaphoreTake(xI2SSemaphore, portMAX_DELAY);
        
        // 选择本次要填充的 DMA 缓冲区(ping/pong 交替)
        if(I2SdmaBuff == 0) target_buf = music_ctrl.i2sbuf1;
        else target_buf = music_ctrl.i2sbuf2;
        
		// 暂停时往缓冲写 0(静音)
		if(Music_Suspend_Flag) 
		{
			memset(target_buf, 0, OUT_BUF_SIZE);
		}
		else 
		{
			// 文件指针被 seek 改动过:重新读入一块压缩数据
			if(flac_fptr != music_ctrl.file->fptr) 
			{
				// 修正越界定位:不得早于音频数据起点
				if(music_ctrl.file->fptr < flacctrl->datastart) {
					f_lseek(music_ctrl.file, flacctrl->datastart);
				}
				f_read(music_ctrl.file, flac_in_buffer, IN_BUF_SIZE, &br);
				in_buf_byte_left = br;// 记录缓冲中可解码的字节数
                in_buf_offset = 0;  // 复位缓冲游标
				flac_fptr = music_ctrl.file->fptr;// 记录当前文件指针
			}
			
			// 用 foxen-flac 解码(输入=压缩数据,输出=int32 PCM)
			if(flacctrl->nchannels == 1) out_buf_byte_left = OUT_BUF_SIZE/2;// 单声道只需填一半(展开双声道时按样本复制)
			else out_buf_byte_left = OUT_BUF_SIZE;// 立体声:填满整块缓冲
		
			uint8_t *out_ptr = target_buf;  // 输出写指针(解码结果直接落在目标 DMA 缓冲里)

			// 循环解码,直到本次目标缓冲被填满
			while(out_buf_byte_left)
			{
				// 当数据余量不到一半时，集中大块填满，减少碎片化耗时
				if(in_buf_byte_left < IN_BUF_SIZE/2) 
				{
                    // 先把剩下未处理的有效数据挪到缓冲区头部
					if(in_buf_byte_left > 0 && in_buf_offset > 0) {
						memmove(flac_in_buffer, flac_in_buffer + in_buf_offset, in_buf_byte_left);
					}
					in_buf_offset = 0; // 重置游标

                    // 一次性把缓存给全部塞满，减少SD卡的极低效碎片调用
					f_read(music_ctrl.file, flac_in_buffer + in_buf_byte_left, IN_BUF_SIZE - in_buf_byte_left, &br);
					in_buf_byte_left += br;
				}
				// 没有数据了
				if(!in_buf_byte_left) {Music_Status = Song_End; break;}
				
				// 传给解码器的是 flac_in_buffer + in_buf_offset
				uint32_t in_len = in_buf_byte_left;// 本次送进解码器的有效字节数
				uint32_t out_len = out_buf_byte_left / 4;// 期望解出的采样点数(int32 计)
				int32_t* temp_buf = (int32_t*)out_ptr;// 就地解码到目标 DMA 缓冲
				
				fx_flac_state_t state = fx_flac_process(flacctrl->decoder, flac_in_buffer + in_buf_offset, &in_len, temp_buf, &out_len);
				
				// 出错
				if(state == FLAC_ERR) {Music_Status = Song_End; break;}// 解码出错,直接结束本曲
                
				// 读取到了数据
				if(out_len > 0) 
				{
					if(flacctrl->nchannels == 1)
					{
						int32_t* src = temp_buf + out_len - 1;// 从尾部倒序搬运,避免覆盖尚未读取的样本
						int32_t* dst = temp_buf + out_len * 2 - 1;// 双声道展开后的写指针
						
						for(uint32_t i = 0; i < out_len; i++)
						{
							int32_t sample = *src;// 取出一个采样点
                            
                            // 定点音量缩放(>>8 与音量系数定标对应)
                            if(kv_hdp0_or_spk1) {
                                sample = (int32_t)(((int64_t)sample * kv_spk_value) >> 8);
                            }
                            
							int32_t ex_sample = __ROR(sample, 16);// 半字交换:适配 I2S 半字装载的字节序
                            
							*dst-- = ex_sample;
							*dst-- = ex_sample;
							src--;
						}
						out_ptr += out_len * 8;// 单声道展开后每采样点占 8 字节
					}
					else
					{
						uint32_t* p = (uint32_t*)temp_buf;// 立体声:就地改写解码结果
						for(uint32_t i = 0; i < out_len; i++)
						{
                            int32_t sample = p[i];
                            
                            // 定点音量缩放(>>8 与音量系数定标对应)
                            if(kv_hdp0_or_spk1) {
                                sample = (int32_t)(((int64_t)sample * kv_spk_value) >> 8);
                            }
                            
                            p[i] = __ROR(sample, 16);// 半字交换(字节序适配)
						}
						out_ptr += out_len * 4;// 立体声每采样点占 4 字节
					}
					out_buf_byte_left -= out_len * 4;// 扣减本次已填充的字节数
				}
				
				// 更新游标指针和剩余量，绝对不在此处进行 memmove！
				if(in_len <= in_buf_byte_left)
				{
                    in_buf_offset += in_len;// 前移读游标(此处绝不能 memmove)
					in_buf_byte_left -= in_len;
				}
				else in_buf_byte_left = 0;
			}

			flac_get_curtime(music_ctrl.file, flacctrl);// 更新当前播放秒数
			flac_fptr = music_ctrl.file->fptr;// 记录当前文件指针
            music_info.current_sec = flacctrl->cursec;// 同步给 UI
        }
		// 送当前缓冲做频谱分析
		Extract_FFT(target_buf);
    } 
	else 
	{
        // 阶段3: 收尾(停止 I2S + 释放全部资源 + 按播放模式切歌)
        I2S_Play_Stop();// 停止播放
        
        // 释放解码器与控制结构体
        if (flacctrl) {
            // 先释放解码器实例
            if (flacctrl->decoder) {
                free_bsc(flacctrl->decoder);
                flacctrl->decoder = NULL; 
            }
            free_bsc(flacctrl);
            flacctrl = NULL;
        }

        // 关闭并释放文件对象
        if (music_ctrl.file) {
            f_close(music_ctrl.file);
            free_bsc(music_ctrl.file);
            music_ctrl.file = NULL;
        }

        // 释放 I2S DMA 缓冲区1(ping)
        if (music_ctrl.i2sbuf1) { free_bsc(music_ctrl.i2sbuf1); music_ctrl.i2sbuf1 = NULL; }
        // 释放 I2S DMA 缓冲区2(pong)
        if (music_ctrl.i2sbuf2) { free_bsc(music_ctrl.i2sbuf2); music_ctrl.i2sbuf2 = NULL; }
        // 释放压缩数据输入缓冲区
        if (flac_in_buffer) { free_bsc(flac_in_buffer); flac_in_buffer = NULL; }
        
        // 处理播放状态转换
        if(Music_Status == Song_Error) Music_Status = Music_Exit;
        if(Music_Status == Song_End) {
            if(kv_music_switch_method == Play_In_Order) play_next_song();
            if(kv_music_switch_method == Play_Randomly) play_random_song();
            if(kv_music_switch_method == Play_Repeatly) play_same_song();
            Music_Status = Song_Prepare;
        }
        if(Music_Status == Song_Next) {
            play_next_song();
            Music_Status = Song_Prepare;
        }
        if(Music_Status == Song_Previous) {
            play_previous_song();
            Music_Status = Song_Prepare;
        }
		if(Music_Status == Song_File)
		{
			play_specific_song(chosen_file_path); 
			chosen_file_path_free();
			Music_Status = Song_Prepare;
		}
    }
}
