#include "stm32f4xx.h"
#include "ogg.h" 
#include "malloc.h"
#include "ff.h"
#include "i2s.h"
#include "string.h"
#include "music.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "variables.h"
#include "defines.h"
#include "spectrum_dsp.h"
#include "file_unit.h"

#include "stb_vorbis.h"

// 控制参数
__oggctrl oggctrl;// OGG播放控制结构体

// stb_vorbis 的静态安全内存池 (85KB, FH=6 优化 + blocksize=2048 够用)
#define OGG_MEMORY_SIZE (70 * 1024)
uint8_t* ogg_mem_pool = NULL;// 解码器内存池首地址(准备时分配,收尾时释放)
stb_vorbis_alloc ogg_alloc;// 交给 stb_vorbis 的分配器描述(指向上面这块内存池)

static volatile uint32_t read_bytes = 0;// 本轮从解码器取到的字节数(不足一整缓冲即判文件尾)

// ==================== 解码与缓冲填充 ====================

// OGG填充DMA缓冲区(向 stb_vorbis 取交错 PCM,原地做音量缩放,并送频谱分析)
// buf: 输出缓冲区指针
// size: 输出缓冲区大小(字节)
// 返回值: 实际解码出的字节数,不足 size 说明已到文件尾
uint32_t ogg_buffill(uint8_t* buf, uint16_t size) 
{
	int16_t *p16_out = (int16_t *)buf;// 输出指针(直接按 16bit 样本写入)
	uint32_t shorts_to_read = size / 2; // 需要的 short 样本数(字节数 / 2)
	
	// 调用 stb_vorbis 解码, 强制输出双声道以匹配 I2S 硬件配置
	// 无论原文件是单声道还是双声道，始终输出 2 声道交错 PCM
	int samples = stb_vorbis_get_samples_short_interleaved(oggctrl.stb_vf, 2, p16_out, shorts_to_read);

	// 转换为总读取字节数 (固定按 2 声道计算)
	uint32_t bytes_read = samples * 2 * 2;// 换算总字节数(2 声道 x 2 字节/样本)
	
	// 数据不足(通常是文件尾):补零以防杂音
	if (bytes_read < size) {
		memset(buf + bytes_read, 0, size - bytes_read);// 尾部清零
	}
	
	// 处理音量缩放 (原地处理)
	if(kv_hdp0_or_spk1)
	{
		uint32_t total_samples = size / 2;// 待处理的 16bit 样本总数
		for (uint32_t i = 0; i < total_samples; i++) 
		{
			int32_t val = ((int32_t)p16_out[i] * kv_spk_value) >> 8;// 定点音量:乘系数后右移 8 位(0dB = 0x100)
			p16_out[i] = (int16_t)val;// 写回原缓冲
		}
	}
	
	Extract_FFT(buf); // 送当前缓冲做频谱分析
	return bytes_read;
}

// ==================== 播放控制 ====================

// OGG 文件快进快退函数(解码器原生 seek,直接按绝对采样帧定位)
// target_sec: 目标秒数
void ogg_file_seek(uint32_t target_sec)
{
	if(!oggctrl.stb_vf) return;// 尚未准备完成时直接忽略
	if(target_sec >= oggctrl.totsec) target_sec = oggctrl.totsec - 1;// 夹紧到合法区间(请求末秒会让解码器落在流尾)
	
	// stb_vorbis 接受绝对采样帧数进行快进
	uint32_t target_sample = target_sec * oggctrl.samplerate;// 目标样本序号 = 秒数 x 采样率
	stb_vorbis_seek(oggctrl.stb_vf, target_sample);// 解码器原生 seek(OGG 自带帧索引,不必按字节比例换算)
}

// OGG 播放准备阶段(申请内存池 + 把文件交给 stb_vorbis + 配置 I2S 双缓冲)
// fname: 文件名
// 返回值: 0表示成功,1表示内存分配失败,2表示文件打开失败,
// 3表示 stb_vorbis 打开失败(文件损坏或内存池不足),4表示采样率不受支持
uint8_t ogg_play_song_prepare(uint8_t* fname)
{
	uint8_t res = 0;// 返回值,0表示成功,其他表示失败
	int error = 0;// stb_vorbis 的错误码输出
	
	// 内存分配(文件对象 + 双 DMA 缓冲 + 解码器内存池)
	music_ctrl.file    = (FIL*)malloc_bsc(sizeof(FIL));// 文件对象
	music_ctrl.i2sbuf1 = malloc_bsc(OGG_I2S_TX_DMA_BUFSIZE);// I2S DMA 缓冲区1(ping)
	music_ctrl.i2sbuf2 = malloc_bsc(OGG_I2S_TX_DMA_BUFSIZE);// I2S DMA 缓冲区2(pong)
	ogg_mem_pool       = malloc_bsc(OGG_MEMORY_SIZE);// 解码器内存池
	music_ctrl.tbuf    = NULL;// OGG 不用中间读缓冲(解码器直接产出 PCM)
	
	// 全部指针都申请成功才继续
	if (!music_ctrl.file || !music_ctrl.i2sbuf1 || !music_ctrl.i2sbuf2 || !ogg_mem_pool) {
		return 1;// 内存分配失败
	}
	
	memset(music_ctrl.file, 0, sizeof(FIL));// 文件对象清零
	res = f_open(music_ctrl.file, (char*)fname, FA_READ);// 以只读方式打开
	if (res != FR_OK) return 2;// 打开失败
	
	// 把内存池交给解码器,它内部的块分配都落在这块静态内存里
	ogg_alloc.alloc_buffer = (char *)ogg_mem_pool;// 内存池首地址
	ogg_alloc.alloc_buffer_length_in_bytes = OGG_MEMORY_SIZE;// 内存池长度(字节)
	
	// 打开文件交给解码器(解码状态由 stb_vorbis 维护)
	oggctrl.stb_vf = stb_vorbis_open_file(music_ctrl.file, 0, &error, &ogg_alloc);// 返回解码句柄,NULL 即失败
	if (!oggctrl.stb_vf) {
		return 3;// 打开失败(文件损坏或内存池不足)
	}
	
	stb_vorbis_info info = stb_vorbis_get_info(oggctrl.stb_vf);// 取出流信息(声道数/采样率)
	oggctrl.nchannels = info.channels;// 通道数量(原文件声明的声道)
	oggctrl.samplerate = info.sample_rate;// 采样率
	oggctrl.bps = 16;// 输出位深固定 16bit
	oggctrl.totsec = stb_vorbis_stream_length_in_seconds(oggctrl.stb_vf);// 整首歌时长(秒)
	
	// 同步到 UI 与总控制
	music_info.total_sec = oggctrl.totsec;// 总秒数
	music_info.bitrate = 112000; // OGG 多为 VBR(无固定码率),这里给 UI 一个参考值
	music_info.samplerate = oggctrl.samplerate;// 采样率
	music_info.bit_depth = oggctrl.bps;// 位深
	music_info.current_sec = 0;// 当前秒数清零
	
	// 配置 I2S(固定 16b 帧)并启动 TX DMA 双缓冲
	I2S2_Init(I2S_Standard_Phillips, I2S_Mode_MasterTx, I2S_CPOL_Low, I2S_DataFormat_16b);
	music_bitdepth = 16;// 记录位深,供 ES9018 与频谱分析使用
	if(I2S2_SampleRate_Set(oggctrl.samplerate)) {// 采样率不在支持的档位内
		return 4;// 采样率设置失败
	}
	I2S2_TX_DMA_Init(music_ctrl.i2sbuf1, music_ctrl.i2sbuf2, OGG_I2S_TX_DMA_BUFSIZE / 2);// 半字数 = 缓冲字节数 / 2
	I2S_Play_Stop();// 预置为停止态,等 Task 填好首帧再启动
	
	return 0;
}

// 播放任务逻辑(由 music.c 的 audio_play_task 按 current_format 分派调用)
// fname: 文件名
void ogg_play_song_task(uint8_t* fname)
{
	// 阶段1: 准备(打开解码器并预填两个缓冲)
	if(Music_Status == Song_Prepare)
	{
		if(ogg_play_song_prepare(fname))// 准备失败:走结束流程转下一首
		{
			Music_Status = Song_Next;// 直接转下一首,不走 Song_End 的播放模式分支
		}
		else 
		{
			// 暂停中:两个缓冲都填 0(静音)
			if(Music_Suspend_Flag)
			{
				memset(music_ctrl.i2sbuf1, 0, OGG_I2S_TX_DMA_BUFSIZE);
				memset(music_ctrl.i2sbuf2, 0, OGG_I2S_TX_DMA_BUFSIZE);
				Music_Status = Song_Playing;
			}
			else
			{
				read_bytes = ogg_buffill(music_ctrl.i2sbuf1, OGG_I2S_TX_DMA_BUFSIZE);// 预填 ping 缓冲
				read_bytes = ogg_buffill(music_ctrl.i2sbuf2, OGG_I2S_TX_DMA_BUFSIZE);// 预填 pong 缓冲
				if(read_bytes != OGG_I2S_TX_DMA_BUFSIZE) {Music_Status = Song_End;}// 不足一整缓冲:已到文件尾
				else {Music_Status = Song_Playing;}// 两个缓冲都填满,进入播放态
			}
			if(Music_Status == Song_Playing) I2S_Play_Start();// 有数据才开始 DMA 传输
		}
	}
	// 阶段2: 播放中(DMA 传输完成中断驱动,ping/pong 轮流补数据)
	else if(Music_Status == Song_Playing)
	{
		xSemaphoreTake(xI2SSemaphore, portMAX_DELAY); // 等待一帧传输完成
		
		// I2SdmaBuff 指出当前正在播的缓冲,这里填另一个(两次传输交替)
		if (I2SdmaBuff)
		{
			if(Music_Suspend_Flag) memset(music_ctrl.i2sbuf2, 0, OGG_I2S_TX_DMA_BUFSIZE);// 暂停:整块静音
			else read_bytes = ogg_buffill(music_ctrl.i2sbuf2, OGG_I2S_TX_DMA_BUFSIZE);// 解码下一帧填 pong
		}
		else
		{
			if(Music_Suspend_Flag) memset(music_ctrl.i2sbuf1, 0, OGG_I2S_TX_DMA_BUFSIZE);// 暂停:整块静音
			else read_bytes = ogg_buffill(music_ctrl.i2sbuf1, OGG_I2S_TX_DMA_BUFSIZE);// 解码下一帧填 ping
		}
		
		if(read_bytes != OGG_I2S_TX_DMA_BUFSIZE && !Music_Suspend_Flag) {Music_Status = Song_End;}// 不足一整缓冲即结束本曲(暂停静音不算)
		
		// 更新当前时间
		if (oggctrl.stb_vf) {
			oggctrl.cursec = stb_vorbis_get_sample_offset(oggctrl.stb_vf) / oggctrl.samplerate;// 已解出的采样帧数 / 采样率 = 当前秒数
			music_info.current_sec = oggctrl.cursec;// 同步给 UI
		}
	}
	else
	{
		// 阶段3: 收尾(停止 I2S + 释放全部资源 + 按播放模式切歌)
		I2S_Play_Stop();// 停止播放
		
		// 释放解码器内部信息
		if(oggctrl.stb_vf) {
			stb_vorbis_close(oggctrl.stb_vf);// 关闭解码器(内存池由本文件负责释放)
			oggctrl.stb_vf = NULL;// 句柄置空防野指针
		}
		
		// 释放 FatFs 句柄
		if (music_ctrl.file) {
			f_close(music_ctrl.file);// 关闭文件
			free_bsc(music_ctrl.file);// 释放文件对象
			music_ctrl.file = NULL;// 指针置空防野指针
		}
		
		// 释放配给 stb_vorbis 的内存池(必须在解码器关闭之后)
		if (ogg_mem_pool) {
			free_bsc(ogg_mem_pool);// 归还内存池
			ogg_mem_pool = NULL;// 指针置空防野指针
		}
		
		// 释放 I2S DMA 缓冲区1(ping)
		if (music_ctrl.i2sbuf1) {
			free_bsc(music_ctrl.i2sbuf1);// 释放 ping 缓冲
			music_ctrl.i2sbuf1 = NULL;// 指针置空防野指针
		}
		
		// 释放 I2S DMA 缓冲区2(pong)
		if (music_ctrl.i2sbuf2) {
			free_bsc(music_ctrl.i2sbuf2);// 释放 pong 缓冲
			music_ctrl.i2sbuf2 = NULL;// 指针置空防野指针
		}
		
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

