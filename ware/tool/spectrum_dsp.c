#include "arm_math.h"
#include "variables.h"
#include "defines.h"
#include "music.h"
#include "i2s.h"
#include "malloc.h" 
#include "spectrum_dsp.h"

#define FFT_LENGTH 512

// 全局暴露给LVGL显示的频段能量值 (0-100)
uint8_t g_spectrum_data[SPECTRUM_NUM] = {0}; 
// 输入缓冲区 (浮点数)
static float32_t *fft_input = NULL;// 输入缓冲区 (浮点数)
static float32_t *fft_output = NULL; // 输出缓冲区 (复数)
static float32_t *fft_mag = NULL;// 输出缓冲区 (幅值)

arm_rfft_fast_instance_f32 rfft;// RFFT实例结构体

static uint8_t sample_refreshed = 0;// 标记是否有新的样本数据需要处理

// 提取音频数据并转换为浮点
// src_buf: 输入缓冲区指针 (PCM数据)
void Extract_FFT(uint8_t *src_buf) 
{
	if(fft_input == NULL || fft_output == NULL) return;
	
	if(sample_refreshed) return;
	
    if (music_bitdepth == 16) 
    {
        int16_t *pcm_16 = (int16_t *)src_buf;
        for (int i = 0; i < FFT_LENGTH; i++) {
			// 将16位PCM数据转换为浮点数，范围[-1.0, 1.0]
            fft_input[i] = (float32_t)pcm_16[i * 2] * 3.0517578125e-5f;
        }
    }
    else 
    {
        uint32_t *pcm_32 = (uint32_t *)src_buf;
        for (int i = 0; i < FFT_LENGTH; i++) {
            int32_t sample = __ROR(pcm_32[i * 2], 16);// 还原半字节序
            fft_input[i] = (float32_t)sample * 4.656612873e-10f; // 将32位PCM数据转换为浮点数，范围[-1.0, 1.0]
        }
    }
	
	sample_refreshed = 1;
}


// 初始化频谱分析器
// 分配内存并初始化FFT实例
void Spectrum_Init(void)
{
    if(fft_input == NULL) fft_input = (float32_t *)malloc_ccm(FFT_LENGTH * sizeof(float32_t));
    if(fft_output == NULL) fft_output = (float32_t *)malloc_ccm(FFT_LENGTH * sizeof(float32_t));
    if(fft_mag == NULL) fft_mag = (float32_t *)malloc_ccm((FFT_LENGTH / 2) * sizeof(float32_t));

    if(fft_input && fft_output && fft_mag) {
        arm_rfft_fast_init_f32(&rfft, FFT_LENGTH);
    }
}

// 释放频谱分析器资源
void Spectrum_Deinit(void)
{
    if(fft_input)  { free_ccm(fft_input);  fft_input = NULL;  }
    if(fft_output) { free_ccm(fft_output); fft_output = NULL; }
    if(fft_mag)    { free_ccm(fft_mag);    fft_mag = NULL;    }
    memset(g_spectrum_data, 0, sizeof(g_spectrum_data));
}

// 频谱分析循环（ui线程调用）
// 返回值: 0表示成功,1表示失败 
uint8_t Spectrum_Loop(void) 
{
    if (!fft_input || !fft_output || !fft_mag) return 1;
	
    if(!sample_refreshed) return 1;
	
    if (Music_Status != Song_Playing) return 1;

	uint32_t fpscr = __get_FPSCR();// 获取当前浮点状态寄存器
    if ((fpscr & (1 << 24)) == 0) __set_FPSCR(fpscr | (1 << 24));// 确保浮点异常屏蔽位被设置，避免在FFT计算中出现异常导致程序中断 
	
	arm_rfft_fast_f32(&rfft, fft_input, fft_output, 0);// 执行快速傅里叶变换 (FFT)
	arm_cmplx_mag_f32(fft_output, fft_mag, FFT_LENGTH / 2);// 计算复数幅值
	
	// 重新分配为 20 个频段 (0 到 255)
	const int bands[SPECTRUM_NUM + 1] = {
		1, 2, 3, 4, 5, 6, 8, 10, 13, 17, 
        22, 28, 36, 46, 60, 78, 100, 130, 170, 210, 255
	}; 

	// 已经为你用公式预先算好的 20 条频谱系数
	// 公式: (100.0f * (2.0f + i)) / ((bands[i+1] - bands[i]) * 256.0f)
	static const float32_t band_multiplier[SPECTRUM_NUM] = {
		0.781250f, 1.171875f, 1.562500f, 1.953125f, 2.343750f, 1.367188f, 
		1.562500f, 1.171875f, 0.976563f, 0.859375f, 0.781250f, 0.634766f, 
		0.546875f, 0.418526f, 0.347222f, 0.301846f, 0.234375f, 0.185547f, 
		0.195313f, 0.182292f
	};
	
	for(int i = 0; i < SPECTRUM_NUM; i++) 
	{
		float32_t energy = 0;// 计算当前频段的能量值
		// 将当前频段的幅值累加
		for(int j = bands[i]; j < bands[i+1]; j++) {
			energy += fft_mag[j];
		}

		// 计算当前频段的平均能量值
		energy *= band_multiplier[i];
		
		//将能量值限制在 0 到 100 的范围内
		if (energy > 100.0f) energy = 100.0f;
		
		// 平滑过渡: 如果当前能量值小于上一次的能量值，则以较慢的速度下降
		uint8_t target_val = (uint8_t)energy;
		if (target_val < g_spectrum_data[i]) {
			g_spectrum_data[i] = (g_spectrum_data[i] > 3) ? (g_spectrum_data[i] - 3) : 0;
		} else {
			g_spectrum_data[i] = (g_spectrum_data[i] + target_val) / 2;
		}
	}
	
	sample_refreshed = 0;
	
	return 0;
}
