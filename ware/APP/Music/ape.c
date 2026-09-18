#include "ape.h"
#include "ff.h"
#include "systick_conf.h"
#include "string.h"
#include "malloc.h"
#include "key.h"
#include "i2s.h"
#include "music.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "variables.h"
#include "defines.h"
#include "spectrum_dsp.h"
#include "file_unit.h"

__apectrl * apectrl;	// APE播放控制结构体

#define AUDIO_MIN(x,y)	((x)<(y)? (x):(y))// 取两数较小值

// APE 解码状态变量 (提取为全局，供 seek 和主循环共享)

static int firstbyte = 0;// 当前帧解码器的首字节偏移
static int bytesconsumed = 0;// 当前帧解码器消耗的字节数
static int currentframe = 0;// 当前播放的帧索引(0~totalframes-1)
static int nblocks = 0;// 当前帧剩余的未解码块数
static int blockstodecode = 0;// 本次循环要解码的块数

// Seek 控制标志
static uint8_t ape_seek_flag = 0;// 1=本轮需要执行 seek 跳转(由 ape_file_seek 置位)
static uint32_t ape_target_sec = 0;// seek 目标秒数(Task 内消费)

// apedecoder.c里面需要的数组 
extern filter_int *filterbuf64;		// apedecoder.c 内的长滤波器缓冲(需 2816 字节),由本文件负责分配 

// 填充PCM数据到DAC(APE 固定走 16bit 路径,直接整块搬进 DMA 缓冲)
// buf: 解码得到的 PCM 数据首地址
// size: pcm数据量(16位为单位)
void ape_fill_buffer(uint16_t* buf, uint16_t size)
{
    // 选择本次要填充的 DMA 缓冲区(ping/pong 交替)
    uint16_t *p = (I2SdmaBuff == 0) ? 
                 (uint16_t*)music_ctrl.i2sbuf1: 
                 (uint16_t*)music_ctrl.i2sbuf2;

    memcpy(p, buf, size * sizeof(uint16_t));// 解码结果与 I2S 要求同构,直接整块拷贝
}

// 原地定点音量处理(逐样本乘 kv_spk_value 再 >>8,不改变采样点数)
// buf: 待处理的 PCM 缓冲(原地改写)
// size: pcm数据量(16位为单位)
void ape_apply_volume_in_place(uint16_t* buf, uint16_t size)
{
    if (!kv_hdp0_or_spk1 || kv_spk_value == 0xFF) return;// 未开音量或音量为 0dB 时无需处理

    // 按 32 位打包:一次处理左右两个 16bit 样本
    uint32_t *p32 = (uint32_t *)buf;
    uint32_t samples_pairs = size / 2;// 样本对数(每对打包在 1 个 32bit 里)

    for (uint32_t i = 0; i < samples_pairs; i++) 
    {
        uint32_t raw = p32[i];// 低16位=左声道,高16位=右声道
        int16_t low = (int16_t)(raw & 0xFFFF);// 左声道样本
        int16_t high = (int16_t)(raw >> 16);// 右声道样本

        int32_t val_low = ((int32_t)low * kv_spk_value) >> 8;// 左声道定点缩放
        int32_t val_high = ((int32_t)high * kv_spk_value) >> 8;// 右声道定点缩放

        p32[i] = (uint32_t)((uint16_t)val_low) | ((uint32_t)((uint16_t)val_high) << 16);// 拼回 32 位写回
    }
}

// APE 解码器上下文与缓冲区(seek 与主循环共享)
struct ape_ctx_t *apex;// APE 解码器上下文(帧/块级状态都在这里)

int bytesinbuffer;// 读缓冲中尚未解码的字节数

uint8_t *ape_readptr;// 读缓冲中的解码游标
uint8_t *ape_buffer;// 压缩数据读缓冲(APE_FILE_BUF_SZ)
int *decoded0;// 解码输出块0
int *decoded1;// 解码输出块1

// 得到当前播放时间(按已解码样本数精确换算,不用文件字节比例,避免时间抖动)
// fx: 文件对象指针
// apectrl: APE控制结构体指针
void ape_get_curtime(FIL*fx,__apectrl *apectrl)
{
	// 采样率已知:按样本数精确换算当前秒数
	if (apex && apex->samplerate) {
		uint32_t samples_played;// 已解码的样本数
		if (currentframe > 0) {
			samples_played = (currentframe - 1) * apex->blocksperframe// 前面整帧的样本数
			               + (apex->currentframeblocks - nblocks);// 加上当前帧内已解出的样本数
			if (samples_played > apex->totalsamples)
				samples_played = apex->totalsamples;
		} else {
			samples_played = 0;
		}
		apectrl->cursec = samples_played / apex->samplerate;// 样本数 / 采样率 = 秒
	} else {
		// 兜底:上下文不可用时退回按文件字节比例估算
		long long fpos=0;
		if(fx->fptr>apectrl->datastart)fpos=fx->fptr-apectrl->datastart;// 已读的音频数据字节数
		apectrl->cursec=fpos*apectrl->totsec/(f_size(fx)-apectrl->datastart);// 已读字节 / 总字节 * 总秒数
	}
}

// 播放某个 APE 文件准备阶段(解析头部 + 补读 seektable + 配置 I2S + 预读首块压缩数据)
// fname: 文件名
// 返回值: 0表示成功,1表示编码方式/版本/位深不受支持(APE 只支持 16bit),
// 其他为 ape_parseheader / f_read 的错误码
uint8_t ape_play_song_prepare(uint8_t* fname)
{
    uint8_t res = 0;// 返回值,0表示成功,其他表示失败
	uint32_t totalsamples;// 总采样点数(用于算总时长)
	
	// 内存分配(滤波缓冲 + 控制结构 + 解码上下文 + 双解码块 + 文件对象 + 双 DMA 缓冲 + 读缓冲)
	filterbuf64=malloc_bsc(2816);// apedecoder 滤波器缓冲
	apectrl=malloc_bsc(sizeof(__apectrl));// APE控制结构体
	apex=malloc_bsc(sizeof(struct ape_ctx_t));// 解码器上下文
	decoded0=malloc_bsc(APE_BLOCKS_PER_LOOP*4);// 解码输出块0(每样本 4 字节)
	decoded1=malloc_bsc(APE_BLOCKS_PER_LOOP*4);// 解码输出块1
	
	music_ctrl.file=(FIL*)malloc_bsc(sizeof(FIL));// 文件对象
	music_ctrl.i2sbuf1=malloc_bsc(APE_BLOCKS_PER_LOOP*4);// I2S DMA 缓冲区1(ping)
	music_ctrl.i2sbuf2=malloc_bsc(APE_BLOCKS_PER_LOOP*4);// I2S DMA 缓冲区2(pong)
	ape_buffer=malloc_bsc(APE_FILE_BUF_SZ);// 压缩数据读缓冲

	// 全部指针都申请成功才继续解析
	if(filterbuf64&&apectrl&&apex&&decoded0&&decoded1&&music_ctrl.file&&music_ctrl.i2sbuf1&&music_ctrl.i2sbuf2&&ape_buffer)
	{ 
		// 上下文/控制结构/双 DMA 缓冲先清零
		memset(apex,0,sizeof(struct ape_ctx_t));// 上下文清零
		memset(apectrl,0,sizeof(__apectrl));// 控制结构清零
		memset(music_ctrl.i2sbuf1,0,APE_BLOCKS_PER_LOOP*4);// DMA 缓冲1清零
		memset(music_ctrl.i2sbuf2,0,APE_BLOCKS_PER_LOOP*4);// DMA 缓冲2清零
		f_open(music_ctrl.file,(char*)fname,FA_READ);// 打开文件
		// 解析文件头(采样率/帧数/每帧块数/首帧位置)
		res=ape_parseheader(music_ctrl.file,apex);
		if(res==0)
		{  
			// APE 的硬约束:高位深/新编码/版本越界一律拒绝(解码器只支持 16bit)
			if((apex->compressiontype>3000)||(apex->fileversion<APE_MIN_VERSION)||(apex->fileversion>APE_MAX_VERSION||apex->bps!=16))
			{
				res = 1;
			}
			else
			{
				apectrl->bps=apex->bps;// 位深(APE 固定 16)
				apectrl->samplerate=apex->samplerate;// 采样率
				// 总样本数 = 末帧块数 + 每帧块数 * (总帧数 - 1)
				if(apex->totalframes>1)totalsamples=apex->finalframeblocks+apex->blocksperframe*(apex->totalframes-1);
				else totalsamples=apex->finalframeblocks;// 只有一帧:直接用末帧块数
				apectrl->totsec=totalsamples/apectrl->samplerate;// 总秒数
				apectrl->bitrate=(f_size(music_ctrl.file)-apex->firstframe)*8/apectrl->totsec;// 平均码率 = 音频数据字节数 * 8 / 总秒数
				apectrl->outsamples=APE_BLOCKS_PER_LOOP*2;// 单轮输出的样本数
				apectrl->datastart=apex->firstframe;// 音频数据起始位置
                
                // seektable 兜底:parser.c 的长度校验过严时不会分配,这里按 junk+descriptor+header 长度手工补读
                if (apex->seektable == NULL && apex->totalframes > 0) {
                    apex->seektable = (uint32_t*)malloc_bsc(apex->totalframes * sizeof(uint32_t));
                    if (apex->seektable) {
                        uint32_t st_pos = apex->junklength + apex->descriptorlength + apex->headerlength;// seektable 的绝对位置
                        f_lseek(music_ctrl.file, st_pos);
                        uint32_t br;// 实际读取的字节数
                        f_read(music_ctrl.file, apex->seektable, apex->totalframes * sizeof(uint32_t), &br);
                    }
                }
                // (seektable 兜底结束)

                music_info.total_sec = apectrl->totsec;// 总秒数
                music_info.bitrate = apectrl->bitrate;// 比特率
                music_info.samplerate = apectrl->samplerate;// 采样率
                music_info.bit_depth = apectrl->bps;// 位深
                music_info.current_sec = 0;// 当前秒数清零
			}
		}
	}
	if(res==0)
	{   
		// 解析成功:配置 I2S(固定 16b 帧)并预读首块压缩数据
		I2S2_Init(I2S_Standard_Phillips,I2S_Mode_MasterTx,I2S_CPOL_Low,I2S_DataFormat_16b);
		I2S2_SampleRate_Set(apex->samplerate);// 设置采样率
		I2S2_TX_DMA_Init(music_ctrl.i2sbuf1,music_ctrl.i2sbuf2,APE_BLOCKS_PER_LOOP*2);// 启动 TX DMA 双缓冲(半字数 = 每轮样本数)
		f_lseek(music_ctrl.file,apex->firstframe);// 跳过文件头,定位到音频数据
		res = f_read(music_ctrl.file,ape_buffer,APE_FILE_BUF_SZ,(uint32_t*)&bytesinbuffer);// 预读首块压缩数据
		ape_readptr = ape_buffer;// 读游标指向读缓冲头部
	}
	return res;
}

// 播放任务逻辑(由 music.c 的 audio_play_task 按 current_format 分派调用)
// fname: 文件名
void ape_play_song_task(uint8_t* fname)
{
	int n;// f_read 实际读入的字节数
	uint8_t res = 0;// 返回值,0表示成功,其他表示失败

	// 阶段1: 准备(解析头部并进入播放态)
	if(Music_Status == Song_Prepare)
	{
		uint8_t res = ape_play_song_prepare(fname);// 解析头部 + 预读首块
		if(!res) 
		{
			// 复位帧级解码状态(每首歌开始时必须清一次)
			currentframe = 0; 
			firstbyte = 3;// 首帧字节偏移(3=从 4 字节对齐块的末尾读起)
			bytesconsumed = 0;// 已消耗字节数清零
			nblocks = 0;// 未解码块数清零
			blockstodecode = 0;// 本轮解码块数清零
			ape_seek_flag = 0;// 清除 seek 标志
			I2S_Play_Start();// 启动播放
			Music_Status = Song_Playing;
		}
		else Music_Status = Song_End;// 准备失败也走结束流程
	}
	// 阶段2: 播放中(DMA 传输完成中断驱动,按帧逐步解码填缓冲)
	else if(Music_Status == Song_Playing)
	{
		xSemaphoreTake(xI2SSemaphore, portMAX_DELAY);// 等待 DMA 传输完成信号量
		
		// 选择本次要填充的 DMA 缓冲区(ping/pong 交替)
		uint16_t* target_buf = (I2SdmaBuff == 0) ? (uint16_t*)music_ctrl.i2sbuf1 : (uint16_t*)music_ctrl.i2sbuf2;

		// 暂停时往缓冲写 0(静音)
		if(Music_Suspend_Flag) 
		{
			memset(target_buf, 0, APE_BLOCKS_PER_LOOP * 4);// 静音:整块清零
		}
		// seek 流程:换算目标帧 → 查 seektable → 重置解码器状态
		else if (ape_seek_flag)
		{
			ape_seek_flag = 0;// 标志已消费,立刻清掉

			if (apex && apex->seektable && apex->totalframes > 0) {
				// 1. 换算目标帧索引，防止越界
				uint32_t target_sample = ape_target_sec * apex->samplerate;// 目标样本序号
				currentframe = target_sample / apex->blocksperframe;// 帧索引 = 目标样本号 / 每帧块数
				if (currentframe >= apex->totalframes)// 越界夹紧到末帧
					currentframe = apex->totalframes > 0 ? apex->totalframes - 1 : 0;

				// 2. 从 seektable 取出对应文件偏移
				uint32_t fpos = apex->seektable[currentframe];// 该帧在文件中的偏移
                
                // 兼容两种 seektable 写法:若存的是相对首帧的偏移,则补上首帧绝对位置
                if (apex->seektable[0] < apex->firstframe) {
                    fpos += apex->firstframe;// 表里存的是相对首帧的偏移
                }

				// 3. 按照 APE 小端 32 位字节对齐规则换算起始字节
				firstbyte = 3 - (fpos & 3);// 小端 32 位对齐:不足 4 字节的部分从块尾读起
				fpos &= ~3;// 向下对齐到 4 字节边界

				// 4. 定位并读取新的数据块
				f_lseek(music_ctrl.file, fpos);
				int n_read;// 实际读取的字节数
				f_read(music_ctrl.file, ape_buffer, APE_FILE_BUF_SZ, (uint32_t*)&n_read);
				bytesinbuffer = n_read;// 刷新剩余字节数
				ape_readptr = ape_buffer;// 读游标复位到缓冲头部

				// 5. 归零计数状态，迫使下一循环进入 init_frame_decoder 重置解码树
				nblocks = 0;// 归零:迫使下轮走 init_frame_decoder 重建解码树
				bytesconsumed = 0;// 已消耗字节数归零
				blockstodecode = 0;// 本轮解码块数归零
			}

			// 6. 将当轮发往 DAC 的 DMA 缓冲区清空，防止断层杂音
			memset(target_buf, 0, APE_BLOCKS_PER_LOOP * 4);// 清空当轮 DMA 缓冲,防止断层杂音

			// 7. 更新时间
			apectrl->cursec = ape_target_sec;// 时间立即跳到目标秒
			music_info.current_sec = apectrl->cursec;// 同步给 UI
		}
		else 
		{
			// 本帧的块已解完:推进到下一帧并重建解码树
			if(nblocks <= 0)	
			{
				if(currentframe < apex->totalframes)// 还有后续帧可解
				{
					if(currentframe==(apex->totalframes-1))nblocks=apex->finalframeblocks;// 末帧用最后的块数
					else nblocks=apex->blocksperframe;// 其余帧块数固定
					apex->currentframeblocks=nblocks;// 记录本帧总块数(时间计算要用)
					init_frame_decoder(apex,ape_readptr,&firstbyte,&bytesconsumed);// 重建解码树(帧头变了必须重来)
					ape_readptr+=bytesconsumed;// 前移读游标
					bytesinbuffer-=bytesconsumed;// 扣减剩余字节数
					currentframe++;// 帧索引前移
				}
				else Music_Status = Song_End;// 已到末帧:播放结束
			}
			// 本帧还有块可解:解一块写进目标缓冲
			if(nblocks>0)
			{
				blockstodecode=AUDIO_MIN(APE_BLOCKS_PER_LOOP,nblocks);// 本轮解码块数(不越过本帧余量)
                
				res =decode_chunk(apex,ape_readptr,&firstbyte,&bytesconsumed,decoded0,decoded1,blockstodecode);// 解码一块,结果落在 decoded0/decoded1
				// 解码失败:立即结束本曲
				if(res!=0)
				{
					Music_Status = Song_End;
				}

				// 只拷贝有效数据并原位处理音量，末尾清零
				ape_fill_buffer((uint16_t*)decoded1, blockstodecode * 2);// 把解码结果搬进 DMA 缓冲
				// 不足一整块:尾部补 0,避免残留上一轮数据
				if(blockstodecode < APE_BLOCKS_PER_LOOP)
				{
					memset(target_buf + blockstodecode * 2, 0, (APE_BLOCKS_PER_LOOP - blockstodecode) * 4);// 尾部清零
				}
                ape_apply_volume_in_place(target_buf, blockstodecode * 2);// 定点音量(原地)
				
                ape_readptr+=bytesconsumed;// 前移读游标
				bytesinbuffer-=bytesconsumed;// 扣减剩余字节数
				// 单块消耗异常(超出一整轮的量)⇒ 判流损坏并结束
				if(bytesconsumed>4*APE_BLOCKS_PER_LOOP)
				{
					nblocks=0;
					Music_Status = Song_End;
				}
				// 余量不足一整块:先挪到缓冲头部再补读
				if(bytesinbuffer<4*APE_BLOCKS_PER_LOOP)
				{ 
					memmove(ape_buffer,ape_readptr,bytesinbuffer);// 残留数据挪到缓冲头部
					res=f_read(music_ctrl.file,ape_buffer+bytesinbuffer,APE_FILE_BUF_SZ-bytesinbuffer,(uint32_t*)&n);// 补读满整个读缓冲
					if(res) Music_Status = Song_End;// 读失败即结束
					bytesinbuffer+=n;// 累加新读入的字节数
					ape_readptr=ape_buffer;// 读游标复位到缓冲头部
				} 
				nblocks-=blockstodecode;// 扣减本帧已解码块数
			}
            
            ape_get_curtime(music_ctrl.file, apectrl);// 更新当前播放秒数
            music_info.current_sec = apectrl->cursec;// 同步给 UI
		}
		// 送当前缓冲做频谱分析
		Extract_FFT((uint8_t*)target_buf);
	}
	else
	{
		// 阶段3: 收尾(停止 I2S + 释放全部资源 + 按播放模式切歌)
		I2S_Play_Stop();// 停止播放
        // 关闭并释放文件对象
        if (music_ctrl.file) {
            f_close(music_ctrl.file);
            free_bsc(music_ctrl.file);
            music_ctrl.file = NULL;
        }

		// 释放 apedecoder 的长滤波缓冲
		if (filterbuf64) { free_bsc(filterbuf64); filterbuf64 = NULL; }
		// 释放控制结构体
		if (apectrl) { free_bsc(apectrl); apectrl = NULL; }
        
        // 释放解码上下文(seektable 必须单独释放)
        if (apex) {
            // seektable 是本文件手工分配的,不能漏
            if (apex->seektable) { free_bsc(apex->seektable); apex->seektable = NULL; }
            free_bsc(apex); 
            apex = NULL;
        }
        
		// 释放解码输出块0
		if (decoded0) { free_bsc(decoded0); decoded0 = NULL; }
		// 释放解码输出块1
		if (decoded1) { free_bsc(decoded1); decoded1 = NULL; }
		// 释放 I2S DMA 缓冲区1(ping)
		if (music_ctrl.i2sbuf1) { free_bsc(music_ctrl.i2sbuf1); music_ctrl.i2sbuf1 = NULL; }
		// 释放 I2S DMA 缓冲区2(pong)
		if (music_ctrl.i2sbuf2) { free_bsc(music_ctrl.i2sbuf2); music_ctrl.i2sbuf2 = NULL; } 
		// 释放压缩数据读缓冲
		if (ape_buffer) { free_bsc(ape_buffer); ape_buffer = NULL; }
		
		ape_buffer = NULL;// 指针置空防野指针
		ape_readptr = NULL;// 读游标置空
		
		// 处理播放状态转换
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

// APE 文件快进快退函数(只记目标秒数 + 置标志,真正跳转在 Task 内完成)
// target_sec: 目标秒数
void ape_file_seek(uint32_t target_sec)
{
	if (!apectrl || !apex) return;// 尚未准备完成时直接忽略
	if (target_sec > apectrl->totsec) target_sec = apectrl->totsec;// 夹紧到合法区间
	ape_target_sec = target_sec;// 记录目标秒数
	ape_seek_flag = 1;// 置标志,交 Task 执行跳转
}
