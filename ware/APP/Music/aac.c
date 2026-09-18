#include "stm32f4xx.h"                  // Device header
#include "aac.h"
#include "music.h"
#include "systick_conf.h"
#include "malloc.h"
#include "ff.h"
#include "string.h"
#include "i2s.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "variables.h"
#include "defines.h"
#include "spectrum_dsp.h"
#include "file_unit.h"

__aacctrl * aacctrl;	    // aac控制结构体 

uint8_t config_err = 0;// 最近一次解析的错误码(供 UI 查询)

// 填充PCM数据到DAC
// buf:PCM数据首地址
// size:pcm数据量(16位为单位)
// nch:声道数(1,单声道,2立体声)
void aac_fill_buffer(uint16_t* buf,uint16_t size,uint8_t nch)
{
	uint16_t i;// 循环变量
	uint16_t *p_out;// 输出指针(指向本次要填充的 DMA 缓冲)
    int16_t *p_in = (int16_t*)buf; // 视为有符号16位数据处理

	// 选择本次要填充的 DMA 缓冲区(ping/pong 交替)
	if(I2SdmaBuff == 0) p_out=(uint16_t*)music_ctrl.i2sbuf1;
	else p_out=(uint16_t*)music_ctrl.i2sbuf2;

    if(kv_hdp0_or_spk1) // 开启音量调节
    {
        if(nch==2)
        {
            for(i=0;i<size;i++) 
            {
                int32_t val = ((int32_t)p_in[i] * kv_spk_value) >> 8;
                p_out[i] = (int16_t)val;
            }
        }
        else	// 单声道
        {
            for (i = 0; i < size; i++) 
            {
                int32_t val = ((int32_t)p_in[i] * kv_spk_value) >> 8;
                p_out[2*i] = (int16_t)val;
                p_out[2*i+1] = (int16_t)val;
            }
        }
    }
    else // 原样输出
    {
        if(nch==2)
        {
            // memcpy效率通常比循环高
            memcpy(p_out, buf, size * sizeof(uint16_t));
        }
        else	// 单声道
        {
            for (i = 0; i < size; i++) 
            {
                p_out[2*i] = buf[i];
                p_out[2*i+1] = buf[i];
            }
        }
    }
} 

// AAC 解码状态(seek 与播放任务共享)
HAACDecoder aacdecoder;// AAC 解码器句柄
AACFrameInfo aacframeinfo;// 刚解码帧的信息
uint8_t* aac_buffer = NULL; // 输入buffer  
uint8_t* aac_readptr = NULL;// AAC解码读指针
int aac_offset = 0;	        // 偏移量
// 丢弃解码缓冲中的残留数据,强制下一次重新读文件
int aac_bytesleft = 0;      // buffer还剩余的有效数据
uint8_t aac_outofdata = 1;// 输入缓冲是否已耗尽(1=需要重新读文件)

// 得到当前播放时间
// fx: 文件对象指针
// aacx: AAC控制结构体指针
void aac_get_curtime(FIL*fx,__aacctrl *aacx)
{
    uint32_t fpos = 0;// 已解码的数据字节偏移
    if(fx->fptr > aacx->datastart) fpos = fx->fptr - aacx->datastart;
    
    uint32_t data_size = f_size(fx) - aacx->datastart;// 音频数据总字节数
    // 按已读字节比例换算当前播放秒数
    if(data_size > 0)
    {
        aacx->cursec = (uint32_t)(((uint64_t)fpos * aacx->totsec) / data_size);
    }
}

// AAC文件快进快退函数(清空解码缓冲,强制下次重新读文件)
// pos: 需要定位到的文件绝对位置
// 返回值: 实际定位到的文件位置
uint32_t aac_file_seek(uint32_t pos)
{
    uint32_t file_size = f_size(music_ctrl.file);// 文件总大小
    if(pos > file_size) pos = file_size;
    if(pos < aacctrl->datastart) pos = aacctrl->datastart;

    f_lseek(music_ctrl.file, pos);
    
    aac_bytesleft = 0;
    if(aac_buffer) aac_readptr = aac_buffer;
    aac_outofdata = 1;

    return f_tell(music_ctrl.file);
}

// AAC文件识别与头部解析(支持 ADIF 裸流头与 ADTS 同步字两种封装)
// pname: 文件名
// pctrl: AAC控制结构体指针
// 返回值: 0表示成功,1表示解码器初始化失败,2表示内存分配失败,3表示文件打开失败,
// 4表示文件读取失败,5表示未找到同步字,6表示试解码失败
uint8_t aac_get_info(uint8_t *pname, __aacctrl* pctrl) 
{
    HAACDecoder decoder = NULL;// 解码器句柄
    AACFrameInfo frame_info;// 试解码得到的帧信息
    FIL *faac = NULL;// 文件对象
    uint8_t *buf = NULL;// 文件头读取缓冲区
    uint32_t br;// 实际读取的字节数
    uint8_t res = 0;// 返回值,0表示成功,其他表示失败
    int offset = -1;// 首个同步字在缓冲区中的偏移
    uint32_t file_size = 0;// 文件总大小
    uint8_t file_opened = 0;// 文件是否已打开(决定收尾时是否 f_close)
    int is_adif = 0;// 是否为 ADIF 裸流(否则按 ADTS 处理)
    uint32_t adif_header_size = 0;// ADIF 头长度

    // 初始化解码器
    decoder = AACInitDecoder();
    if (!decoder) res = 1;

    // 分配文件对象与读取缓冲区
    if (!res) {	
        faac = malloc_bsc(sizeof(FIL));
        buf = malloc_bsc(AAC_FILE_BUF_SZ);
        if (!faac || !buf) res = 2;
    }

    // 打开文件
    if (!res) {
        if (f_open(faac, (const char*)pname, FA_READ) != FR_OK) res = 3;
    }

    // 读取文件头(用于格式探测)
    if (!res) {
        file_opened = 1;
        file_size = f_size(faac);// 文件总大小
        uint32_t read_size = AAC_FILE_BUF_SZ < file_size ? AAC_FILE_BUF_SZ : file_size;// 本次要读取的字节数
        if (f_read(faac, buf, read_size, &br) != FR_OK || br < 1024) res = 4;
    }

    if (!res) 
	{
        // 优先按 ADIF 裸流头解析
        if (br >= 4 && memcmp(buf, "ADIF", 4) == 0) {
            is_adif = 1;
            uint8_t* ptr = buf + 4;// 指向 ADIF 头首个字段
            adif_header_size = 4;

            uint8_t flags = *ptr++;// 第1字节: 标志位
            adif_header_size++;
            uint8_t bitstream_type = (flags >> 4) & 0x01;// bit4: 0=按码率字段(CBR),1=按节目流(VBR)

            if (bitstream_type == 0) {
                if (ptr + 3 <= buf + br) {
                    pctrl->bitrate = (ptr[0] << 16) | (ptr[1] << 8) | ptr[2];// 大端24位码率(bps)
                    ptr += 3;
                    adif_header_size += 3;// 码率字段占3字节
                }
            }
            pctrl->datastart = adif_header_size;
            offset = adif_header_size;// 数据从 ADIF 头之后开始
        } else 
		{
            // 非 ADIF,搜索 ADTS 同步字作为解码起点
            offset = AACFindSyncWord(buf, br);
        }

        if (offset < 0 && !is_adif) res = 5;// 未找到同步字
    }

    // 试解码一帧,验证数据可用并取帧参数
    if (!res) {
        unsigned char *p_work;// 解码输入指针
        int valid_bytes;// 可解码的有效字节数
        int pre_valid_bytes;// 解码前的有效字节数

        if (is_adif) {
            p_work = buf + adif_header_size;
            valid_bytes = br - adif_header_size;
        } else {
            p_work = buf + offset;
            valid_bytes = br - offset;
            pctrl->datastart = offset;
        }

        pre_valid_bytes = valid_bytes;// 记录解码前的字节数

        if (AACDecode(decoder, &p_work, &valid_bytes, (short*)music_ctrl.tbuf) != 0) {// 解码失败
            res = 6;
        } else {
            AACGetLastFrameInfo(decoder, &frame_info);

            if (is_adif && pctrl->bitrate == 0) 
                pctrl->bitrate = frame_info.bitRate;

            pctrl->nChans = frame_info.nChans;// 声道数
            pctrl->Samplerate_Core = frame_info.sampRateCore;// 核心采样率
            pctrl->Samplerate_Out = frame_info.sampRateOut;// 输出采样率(SBR 上采样后)
            pctrl->bit_depth = frame_info.bitsPerSample;// 位深
            pctrl->outputSamps = frame_info.outputSamps;// 单帧输出采样点数(含全部声道)

            // ADTS 流无码率字段: 优先取帧信息,取不到再按帧长反推
            if (!is_adif) {
                if (frame_info.bitRate > 0) {
                    pctrl->bitrate = frame_info.bitRate;
                } else {
                    int consumed_bytes = pre_valid_bytes - valid_bytes; // 刚才解码吃掉了多少字节
                    uint32_t samps_per_frame = 1024; // AAC默认单通道单帧采样点
                    if (pctrl->nChans > 0) {
                        samps_per_frame = pctrl->outputSamps / pctrl->nChans;
                    }
                    if (consumed_bytes > 0 && pctrl->Samplerate_Out > 0 && samps_per_frame > 0) {
                        // 码率(bps) = 帧字节 * 8 * 采样率 / 单帧采样数
                        pctrl->bitrate = (uint32_t)((uint64_t)consumed_bytes * 8 * pctrl->Samplerate_Out / samps_per_frame);
                    }
                }
            }

            // 总时长(秒) = 音频数据字节数 * 8 / 码率
            if (pctrl->bitrate > 0) {
                uint32_t data_size = file_size - pctrl->datastart;// 音频数据字节数
                pctrl->totsec = (data_size * 8) / pctrl->bitrate;
            } else {
                pctrl->totsec = 0;
            }
        }
    }

    // 释放临时资源
    if (decoder) AACFreeDecoder(decoder);
    if (faac) {
        if (file_opened) f_close(faac);
        free_bsc(faac);
    }
    if (buf) free_bsc(buf);
    config_err = res;// 记录错误码供 UI 查询
    return res;
}

// 播放某个 AAC 文件准备阶段(解析头部 + 配置 I2S + 打开文件)
// fname: 文件名
// 返回值: 0表示成功,其他表示失败(见 aac_get_info 的返回值说明)
uint8_t aac_play_song_prepare(uint8_t* fname)
{ 
	uint8_t res = 0;// 返回值,0表示成功,其他表示失败
 	// 内存分配(控制结构 + 解码缓冲 + 文件对象 + 双 DMA 缓冲 + 临时缓冲)
 	aacctrl=malloc_bsc(sizeof(__aacctrl));// AAC控制结构体
	aac_buffer = malloc_bsc(AAC_FILE_BUF_SZ); 	// 申请解码buf大小
	music_ctrl.file=(FIL*)malloc_bsc(sizeof(FIL));// 文件对象
	music_ctrl.i2sbuf1=malloc_bsc(2048*2*sizeof(uint16_t));// I2S DMA 缓冲区1(ping)
	music_ctrl.i2sbuf2=malloc_bsc(2048*2*sizeof(uint16_t));// I2S DMA 缓冲区2(pong)
	music_ctrl.tbuf=malloc_bsc(2048*2*sizeof(uint16_t));// 临时解码输出缓冲区
	
	if(!aacctrl||!aac_buffer||!music_ctrl.file||!music_ctrl.i2sbuf1||!music_ctrl.i2sbuf2||!music_ctrl.tbuf)// 内存申请失败
	{
		free_bsc(aacctrl);
		free_bsc(aac_buffer);
		free_bsc(music_ctrl.file);
		free_bsc(music_ctrl.i2sbuf1);
		free_bsc(music_ctrl.i2sbuf2);
		free_bsc(music_ctrl.tbuf); 
		res = 1;
	}
	else memset(music_ctrl.file, 0, sizeof(FIL)); 
	if(res==0)
	{
		memset(music_ctrl.i2sbuf1,0,2048*2*sizeof(uint16_t));	// 数据清零 
		memset(music_ctrl.i2sbuf2,0,2048*2*sizeof(uint16_t));	// 数据清零 
		memset(aacctrl,0,sizeof(__aacctrl));// 数据清零
		res = aac_get_info(fname,aacctrl);// 解析文件信息
	}
	if(res==0)
	{ 
		// 配置 I2S(数据格式 + 采样率 + TX DMA) + 初始化解码器 + 打开文件
		I2S2_Init(I2S_Standard_Phillips,I2S_Mode_MasterTx,I2S_CPOL_Low,I2S_DataFormat_16bextended);	
		I2S2_SampleRate_Set(aacctrl->Samplerate_Out);		// 设置采样率 
		I2S2_TX_DMA_Init(music_ctrl.i2sbuf1,music_ctrl.i2sbuf2,aacctrl->outputSamps);// 配置TX DMA
		aacdecoder=AACInitDecoder(); 					// aac解码申请内存
		res = f_open(music_ctrl.file,(char*)fname,FA_READ);	// 打开文件

        // 填充 music_info 结构体
        music_info.total_sec = aacctrl->totsec;    // 总秒数
        music_info.bitrate = aacctrl->bitrate;     // 比特率
        music_info.samplerate = aacctrl->Samplerate_Out; // 采样率
        music_info.bit_depth = aacctrl->bit_depth;       // 位深
        music_info.current_sec = 0;                // 当前秒数清零
	}
	if(!res && aacdecoder)// 打开文件成功
	{
		f_lseek(music_ctrl.file,aacctrl->datastart);	// 跳过文件头中tag信息
	}
	
	// 准备失败时收尾(关闭并释放文件对象)
	if(res)
	{
		f_close(music_ctrl.file);
		free_bsc(music_ctrl.file); 
		music_ctrl.file = NULL;
	}
	return res;
}

// 播放任务逻辑(由 music.c 的 audio_play_task 按 current_format 分派调用)
// fname: 文件名
void aac_play_song_task(uint8_t* fname)
{
	uint8_t res = 0;// 文件读取返回值
	uint32_t br = 0;// 实际读取的字节数
	int err = 0;// 解码返回值

	// 阶段1: 准备(解析头部并启动播放)
	if(Music_Status == Song_Prepare)
	{
        if (aac_play_song_prepare(fname) == 0) 
		{
             Music_Status = Song_Playing;
			 I2S_Play_Start();
        }
		else Music_Status = Song_End;
    }
	// 阶段2: 播放中(DMA 完成中断驱动 ping-pong 交替解码填缓冲)
	else if(Music_Status == Song_Playing)
	{
		xSemaphoreTake(xI2SSemaphore, portMAX_DELAY);// 传输完成
		
		// 确定目标缓冲区
		uint16_t* target_buf;// 本次要填充的目标缓冲区
		if(I2SdmaBuff == 0)
			target_buf = (uint16_t*)music_ctrl.i2sbuf1;
		else
			target_buf = (uint16_t*)music_ctrl.i2sbuf2;
		
		if(Music_Suspend_Flag) // 暂停状态，填充0
		{
			memset(target_buf, 0, 2048 * 2 * sizeof(uint16_t));
		}
		else // 正常播放状态
		{
			// 解码缓冲用尽,重新从文件读入一大块
			if(aac_outofdata)
			{
				aac_readptr = aac_buffer;	// AAC读指针指向buffer
				aac_offset = 0;		// 偏移量为0
				aac_bytesleft = 0;
				aac_outofdata = 0;
				res=f_read(music_ctrl.file,aac_buffer,AAC_FILE_BUF_SZ,&br);// 一次读取AAC_FILE_BUF_SZ字节
				if(res)// 读数据出错了
				{
					Music_Status = Song_End;
				}
				if(br == 0)		// 读数为0,说明解码完成了.
				{
					Music_Status = Song_End;
				}
				aac_bytesleft += br;// buffer里面有多少有效AAC数据
				err = 0;
			}
			
			if(Music_Status == Song_Playing) // 检查状态是否改变
			{
				aac_offset = AACFindSyncWord(aac_readptr,aac_bytesleft);// 在readptr位置,开始查找同步字符
				if(aac_offset<0)	// 没有找到同步字符,跳出帧解码循环
				{ 
					aac_outofdata = 1;
				}
				else	        // 找到同步字符了
				{
					aac_readptr += aac_offset;		// AAC读指针偏移到同步字符处.
					aac_bytesleft -= aac_offset;		// buffer里面的有效数据个数,必须减去偏移量
					
                    int pre_bytesleft = aac_bytesleft; // 记录解码前字节数

					err = AACDecode(aacdecoder,&aac_readptr,&aac_bytesleft,(short*)music_ctrl.tbuf);// 解码一帧AAC数据
					if(err!=0)
					{
						aac_outofdata = 1;
					}
					else
					{
						AACGetLastFrameInfo(aacdecoder,&aacframeinfo);	// 得到刚刚解码的AAC帧信息
						
                        if(aacframeinfo.bitRate > 0)
                        {
                            if(aacctrl->bitrate != aacframeinfo.bitRate) 
                            {
                                aacctrl->bitrate = aacframeinfo.bitRate; 
                            }
                        }
                        else
                        {
                            // 如果得不到合法码率，同样通过帧长计算更新实时码率
                            int consumed_bytes = pre_bytesleft - aac_bytesleft;
                            uint32_t samps_per_frame = 1024;
                            if (aacframeinfo.nChans > 0) {
                                samps_per_frame = aacframeinfo.outputSamps / aacframeinfo.nChans;
                            }
                            if (consumed_bytes > 0 && aacctrl->Samplerate_Out > 0 && samps_per_frame > 0) {
                                uint32_t cur_bitrate = (uint32_t)((uint64_t)consumed_bytes * 8 * aacctrl->Samplerate_Out / samps_per_frame);
                                if (aacctrl->bitrate != cur_bitrate) {
                                    aacctrl->bitrate = cur_bitrate;
                                }
                            }
                        }

						aac_fill_buffer((uint16_t*)music_ctrl.tbuf,aacframeinfo.outputSamps,aacframeinfo.nChans);// 填充pcm数据
					}
					
					if(aac_bytesleft < AAC_MAINBUF_SIZE*2)// 当数组内容小于2倍MAINBUF_SIZE的时候,必须补充新的数据进来.
					{
						memmove(aac_buffer,aac_readptr,aac_bytesleft);// 移动readptr所指向的数据到buffer里面,数据量大小为:bytesleft
						f_read(music_ctrl.file,aac_buffer+aac_bytesleft,AAC_FILE_BUF_SZ-aac_bytesleft,&br);// 补充余下的数据
						if(br<AAC_FILE_BUF_SZ-aac_bytesleft)
						{
							memset(aac_buffer+aac_bytesleft+br,0,AAC_FILE_BUF_SZ-aac_bytesleft-br); 
						}
						aac_bytesleft = AAC_FILE_BUF_SZ;  
						aac_readptr=aac_buffer; 
					}

                    aac_get_curtime(music_ctrl.file, aacctrl);
                    music_info.current_sec = aacctrl->cursec;
                    music_info.bitrate = aacctrl->bitrate; // 保证UI处能拿取到动态或算好的码率
				}
			}
		}
		// 送当前缓冲做频谱分析
		Extract_FFT((uint8_t*)target_buf);
	}
	else // 阶段3: 收尾(停止 I2S + 释放全部资源 + 按播放模式切歌)
	{
		I2S_Play_Stop();
		
        if (aacdecoder) {
            AACFreeDecoder(aacdecoder);
            aacdecoder = NULL;
        }
		if (music_ctrl.file) {
            f_close(music_ctrl.file);
            free_bsc(music_ctrl.file);
            music_ctrl.file = NULL;
        }
		if (aacctrl) { 
			free_bsc(aacctrl); 
			aacctrl = NULL; 
		}
		if (aac_buffer) { 
			free_bsc(aac_buffer); 
			aac_buffer = NULL; 
		}
		if (music_ctrl.i2sbuf1) { 
			free_bsc(music_ctrl.i2sbuf1); 
			music_ctrl.i2sbuf1 = NULL; 
		}
		if (music_ctrl.i2sbuf2) { 
			free_bsc(music_ctrl.i2sbuf2); 
			music_ctrl.i2sbuf2 = NULL; 
		}
		if (music_ctrl.tbuf) { 
			free_bsc(music_ctrl.tbuf); 
			music_ctrl.tbuf = NULL; 
		}

		aac_buffer = NULL;
		aac_readptr = NULL;
		aac_bytesleft = 0;
		aac_offset = 0;
		aac_outofdata = 1;
		
		if(Music_Status == Song_Error) Music_Status = Music_Exit;
		if(Music_Status == Song_End) 
		{
			if(kv_music_switch_method == Play_In_Order) play_next_song();
			if(kv_music_switch_method == Play_Randomly) play_random_song();
			if(kv_music_switch_method == Play_Repeatly) play_same_song();
			Music_Status = Song_Prepare;
		}
		if(Music_Status == Song_Next)  
		{
			play_next_song();
			Music_Status = Song_Prepare;
		}
		if(Music_Status == Song_Previous)  
		{
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
