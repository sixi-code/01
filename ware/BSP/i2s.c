#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "variables.h"
#include "defines.h"

//理论最佳音质应只需 24bit*48khz*2声道 = 2304kbps（2.304Mhz）
// dma buffer 指示
volatile uint8_t I2SdmaBuff = 0; //此时应该填充buf[I2SdmaBuff]

/*I2S2初始化函数
I2S_Standard(通信标准):可选参数 I2S_Standard_Phillips（菲利普斯标准）,I2S_Standard_MSB（MSB标准）,I2S_Standard_LSB（LSB标准）,I2S_Standard_PCMShort（PCM短帧）,I2S_Standard_PCMLong（PCM长帧）
I2S_Mode(工作模式):可选参数 I2S_Mode_SlaveTx（从机发送）,I2S_Mode_SlaveRx（从机接收）,I2S_Mode_MasterTx（主机发送）,I2S_Mode_MasterRx（主机接收）
I2S_Clock_Polarity(时钟极性):可选参数 I2S_CPOL_Low（低电平）,I2S_CPOL_High（高电平）
I2S_DataFormat(数据格式):可选参数 I2S_DataFormat_16b（16位）,I2S_DataFormat_16bextended（16位扩展）,(frame=32bit);I2S_DataFormat_24b（24位）,I2S_DataFormat_32b（32位）
*/
void I2S2_Init(uint16_t I2S_Standard, uint16_t I2S_Mode,uint16_t Clock_Polarity,uint16_t DataFormat)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    I2S_InitTypeDef I2S_InitStruct;

    // 1. 使能时钟
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB | RCC_AHB1Periph_GPIOC, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);

    // 2. 配置GPIO复用功能
    // PB9(WS), PC3(SD), PB10(CK)
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_9 | GPIO_Pin_10;  // PB9(WS), PB10(CK)
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_3;  // PC3(SD)
    GPIO_Init(GPIOC, &GPIO_InitStruct);

    // 引脚复用映射
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource9, GPIO_AF_SPI2);   // PB9 -> I2S2_WS
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource10, GPIO_AF_SPI2);  // PB10 -> I2S2_CK
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource3, GPIO_AF_SPI2);   // PC3 -> I2S2_SD

    // 3. 复位SPI/I2S
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_SPI2, ENABLE);
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_SPI2, DISABLE);

    // 4. 配置I2S参数
    I2S_InitStruct.I2S_Mode = I2S_Mode;// 设置工作模式
    I2S_InitStruct.I2S_Standard = I2S_Standard;// 设置通信标准
    I2S_InitStruct.I2S_DataFormat = DataFormat;// 设置数据格式
    I2S_InitStruct.I2S_MCLKOutput = I2S_MCLKOutput_Disable;// 禁用MCLK输出
    I2S_InitStruct.I2S_AudioFreq = I2S_AudioFreq_Default; // 默认频率，后续通过PSC设置
    I2S_InitStruct.I2S_CPOL = Clock_Polarity;// 设置时钟极性
    I2S_Init(SPI2, &I2S_InitStruct);
	
    // 5. 使能I2S DMA请求
    SPI_I2S_DMACmd(SPI2, SPI_I2S_DMAReq_Tx, ENABLE);
	
	// 6.如果没创建互斥锁,创建
	if(xI2SSemaphore == NULL) xI2SSemaphore = xSemaphoreCreateBinary();
}

/*
采样率计算公式:Fs=I2SxCLK/[256*(2*I2SDIV+ODD)]
I2SxCLK=(HSE/PLLI2SM)*PLLI2SN/PLLI2SR
【第 1 步：外部晶振】
┌─────────────────────┐
│   HSE 外部晶振      │
│   （通常为 8 MHz）   │
└──────────┬──────────┘
           │
           ▼
【第 2 步：进入专用音频 PLL 进行预处理】
┌─────────────────────────────────────────────┐
│ ① 输入分频： / PLLI2SM（M 通常设为 8）      │
│    → 得到 VCO 输入频率 = 8MHz / 8 = 1MHz    │
└──────────┬──────────────────────────────────┘
           ▼
┌─────────────────────────────────────────────┐
│ ② 主倍频： × PLLI2SN（N 范围 192~432）      │
│    → 得到 VCO 输出频率 = 1MHz × N           │
│    （结果必须在 192MHz ~ 432MHz 之间）       │
└──────────┬──────────────────────────────────┘
           ▼
┌─────────────────────────────────────────────┐
│ ③ 输出分频： / PLLI2SR（R 范围 2~7）        │
│    → 最终得到 I2SxCLK = 1MHz × N / R        │
└──────────┬──────────────────────────────────┘
           │
           ▼
【第 3 步：进入 I2S 外设内部主分频器（寄存器 I2SPR）】
┌─────────────────────────────────────────────────────────┐
│ 主分频系数 DIV = ( 2 × I2SDIV + ODD )                  │
│                                                         │
│  经过这一步，同时得到了 MCK 和采样率 Fs：               │
│  • MCK（主时钟） = I2SxCLK / DIV                       │
│  • 采样率 Fs    = MCK / 256                            │
│    （因为 MCK 恒等于 256 × Fs）                        │
└──────────┬──────────────────────────────────────────────┘
           │
           ▼
【第 4 步：分两路输出】

     ┌─────────────────────┐                  ┌─────────────────────┐
     │  路 ①：MCK 直接输出  │                  │  路 ②：生成 CK      │
     └──────────┬──────────┘                  └──────────┬──────────┘
                │                                        │
                ▼                                        ▼
     ┌─────────────────────┐                  ┌─────────────────────────┐
     │  MCK 引脚输出        │                  │  I2S 硬件位时钟发生器   │
     │  （提供给外部 Codec） │                  │  （根据帧格式自动分频）  │
     └─────────────────────┘                  └──────────┬──────────────┘
                                                         │
                                                         ▼
                                              ┌─────────────────────┐
                                              │  CK（位时钟）引脚   │
                                              │  频率计算公式：     │
                                              │  CK = Fs × 每帧位数 │
                                              └─────────────────────┘
*/

// 采样率/10,PLLI2SN（输出频率）,PLLI2SR（输出分频）,I2SDIV（主分频）,ODD（奇偶）
#define I2S_PSC_ROW_COUNT  (sizeof(I2S_PSC_TBL) / sizeof(I2S_PSC_TBL[0]))
const uint16_t I2S_PSC_TBL[][5]=
{
    {800 , 256,5,12,1},        //8Khz采样率
    {1102, 429,4,19,0},        //11.025Khz采样率 
    {1600, 213,2,13,0},        //16Khz采样率
    {2205, 429,4, 9,1},        //22.05Khz采样率
    {2400, 295,2,12,0},        //24Khz采样率
    {3200, 213,2, 6,1},        //32Khz采样率
    {4410, 271,2, 6,0},        //44.1Khz采样率
    {4800, 258,3, 3,1},        //48Khz采样率
    {8820, 316,2, 3,1},        //88.2Khz采样率
    {9600, 344,2, 3,1},        //96Khz采样率
    {17640,361,2, 2,0},        //176.4Khz采样率 
    {19200,393,2, 2,0},        //192Khz采样率
};



// 采样率配置函数 samplerate（采样率）单位为Hz 返回0表示成功,1表示不支持的采样率
uint8_t I2S2_SampleRate_Set(uint32_t samplerate)
{
    uint8_t i=0; 
    uint32_t tempreg=0;//临时寄存器值
    samplerate/=10;//缩小10倍   
    
    for(i=0;i<I2S_PSC_ROW_COUNT;i++)//看看改采样率是否可以支持（I2S_PSC_ROW_COUNT是行数）
        if(samplerate==I2S_PSC_TBL[i][0])break;

    RCC_PLLI2SCmd(DISABLE);//先关闭PLLI2S
    if(i==I2S_PSC_ROW_COUNT)return 1;//搜遍了也找不到
    RCC_PLLI2SConfig((uint32_t)I2S_PSC_TBL[i][1],(uint32_t)I2S_PSC_TBL[i][2]);//配置PLLI2N参数和PLLI2R参数
 
    RCC->CR|=1<<26;                    //开启I2S时钟
    while((RCC->CR&1<<27)==0);        //等待I2S时钟开启成功. 
    tempreg=I2S_PSC_TBL[i][3]<<0;    //设置I2SDIV
    tempreg|=I2S_PSC_TBL[i][4]<<8;    //设置ODD位
    tempreg|=1<<9;                    //使能MCKOE位,输出MCK
    SPI2->I2SPR=tempreg;            //设置I2SPR寄存器 
    return 0;
}

// DMA双缓冲初始化 buf0:缓冲区1 地址 buf1:缓冲区2 地址 num:每个缓冲区的大小(单位:半字)
void I2S2_TX_DMA_Init(uint8_t* buf0, uint8_t* buf1, uint16_t num)
{
    DMA_InitTypeDef DMA_InitStruct;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE);

    DMA_DeInit(DMA1_Stream4);//复位DMA1_Stream4
    while(DMA_GetCmdStatus(DMA1_Stream4) != DISABLE);//等待DMA1_Stream4复位完成

    DMA_InitStruct.DMA_Channel = DMA_Channel_0;
    DMA_InitStruct.DMA_PeripheralBaseAddr = (uint32_t)&SPI2->DR;
    DMA_InitStruct.DMA_Memory0BaseAddr = (uint32_t)buf0;
    DMA_InitStruct.DMA_DIR = DMA_DIR_MemoryToPeripheral;
    DMA_InitStruct.DMA_BufferSize = num;
    DMA_InitStruct.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStruct.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStruct.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStruct.DMA_MemoryDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStruct.DMA_Mode = DMA_Mode_Circular;//循环模式(保证音频数据连续输出)
    DMA_InitStruct.DMA_Priority = DMA_Priority_VeryHigh;//非常高优先级（防止音频卡顿）
    DMA_InitStruct.DMA_FIFOMode = DMA_FIFOMode_Disable;
    DMA_InitStruct.DMA_FIFOThreshold = DMA_FIFOThreshold_1QuarterFull;
    DMA_InitStruct.DMA_MemoryBurst = DMA_MemoryBurst_Single;
    DMA_InitStruct.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
    DMA_Init(DMA1_Stream4, &DMA_InitStruct);

    // 双缓冲配置
    DMA_DoubleBufferModeConfig(DMA1_Stream4, (uint32_t)buf1, DMA_Memory_0);
    DMA_DoubleBufferModeCmd(DMA1_Stream4, ENABLE);

    // 中断配置
    DMA_ITConfig(DMA1_Stream4, DMA_IT_TC, ENABLE);
}
// 中断处理函数
void DMA1_Stream4_IRQHandler(void)
{
    if(DMA_GetITStatus(DMA1_Stream4, DMA_IT_TCIF4))
    {
        DMA_ClearITPendingBit(DMA1_Stream4, DMA_IT_TCIF4);
		
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	
		if (DMA1_Stream4->CR & (1 << 19)) I2SdmaBuff = 0;//DMA CR寄存器的19位是CT位,1表示当前使用Memory1,0表示当前使用Memory0
		else  I2SdmaBuff = 1;
		xSemaphoreGiveFromISR(xI2SSemaphore, &xHigherPriorityTaskWoken);
		portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

// 启动I2S播放
void I2S_Play_Start(void) 
{ 
    DMA_Cmd(DMA1_Stream4, ENABLE);   // 先开 DMA
    I2S_Cmd(SPI2, ENABLE);           // 后开 I2S
}
// 停止I2S播放
void I2S_Play_Stop(void) 
{ 
    DMA_Cmd(DMA1_Stream4, DISABLE); 
    I2S_Cmd(SPI2, DISABLE);          // 同步关闭 I2S
}
