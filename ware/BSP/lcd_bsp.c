#include "stm32f4xx.h"
#include "systick_conf.h"
#include "FreeRTOS.h"
#include "defines.h"
#include "variables.h"

#define USE_HORIZONTAL 1 //屏幕方向 0-竖屏 1-横屏 2-竖屏翻转 3-横屏翻转

#define LCD_W 240 //屏幕宽度
#define LCD_H 240//屏幕高度

//引脚控制
#define LCD_RES_Low()  GPIOB->BSRR = (uint32_t)GPIO_Pin_12 << 16// LCD复位引脚低电平 复位状态
#define LCD_RES_High()  GPIOB->BSRR = (uint32_t)GPIO_Pin_12// LCD复位引脚高电平 解除复位状态
#define LCD_DC_Low()   GPIOA->BSRR = (uint32_t)GPIO_Pin_4 << 16// LCD数据/命令选择引脚低电平 命令模式
#define LCD_DC_High()   GPIOA->BSRR = (uint32_t)GPIO_Pin_4// LCD数据/命令选择引脚高电平 数据（像素）模式
#define LCD_BLK_Low()  GPIOC->BSRR = (uint32_t)GPIO_Pin_6 << 16// LCD背光引脚低电平 点亮模式
#define LCD_BLK_High()  GPIOC->BSRR = (uint32_t)GPIO_Pin_6// LCD背光引脚高电平 熄灭模式
/* F8T16 
写入数据后添加F8T16 
写命令加F8T16前 
（该 LCD 驱动使用 SPI 16 位数据宽度（SPI_DataSize_16b），每次硬件传输 16 位。
但 LCD 控制器的协议是8 位命令/参数，所以需要用 16 位来"包装" 8 位数据，另一字节填充 F8T16（即 0x00）。）
*/
#define F8T16 0x00

static uint8_t lcd_dma_user = 0; // DMA 传输发起者标识（用于区分不同任务的 LCD 操作）
// LCD GPIO SPI DMA 初始化函数
void LCD_GPIO_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    SPI_InitTypeDef SPI_InitStructure;
    DMA_InitTypeDef DMA_InitStructure;
    
    // 启用新的GPIO和SPI时钟
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA|RCC_AHB1Periph_GPIOB|RCC_AHB1Periph_GPIOC, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);  // SPI1使用DMA2
    
    // LCD控制引脚初始化
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;  // LCD_RST -> PC6
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOC, &GPIO_InitStructure);
    GPIO_SetBits(GPIOC, GPIO_Pin_6);// 复位引脚高电平，解除复位状态

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;  // LCD_DC -> PA4
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    GPIO_SetBits(GPIOA, GPIO_Pin_4);// 数据/命令选择引脚高电平，数据模式

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12; // LCD_BLK -> PB12
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    GPIO_SetBits(GPIOB, GPIO_Pin_12);// 背光引脚高电平，点亮模式
    
    // SPI引脚配置 (SPI1_SCK->PA5, SPI1_MOSI->PA7)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource5, GPIO_AF_SPI1);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource7, GPIO_AF_SPI1);
    
    // SPI配置为16位模式
    SPI_InitStructure.SPI_Direction = SPI_Direction_1Line_Tx;
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_16b;
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_High;
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_2Edge;
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_2;  // 42MHz @ 84MHz PCLK2
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial = 7;
    SPI_Init(SPI1, &SPI_InitStructure);

    // DMA配置 - SPI1_TX使用DMA2 Stream3/Stream5 (这里选择Stream5)
    DMA_DeInit(DMA2_Stream5);
    DMA_InitStructure.DMA_Channel = DMA_Channel_3;  // SPI1_TX使用通道3
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&(SPI1->DR);
    DMA_InitStructure.DMA_Memory0BaseAddr = 0;
    DMA_InitStructure.DMA_DIR = DMA_DIR_MemoryToPeripheral;
    DMA_InitStructure.DMA_BufferSize = 0;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;
    DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Disable;
    DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_HalfFull;
    DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_Single;
    DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
    DMA_Init(DMA2_Stream5, &DMA_InitStructure);
    
    DMA_ITConfig(DMA2_Stream5, DMA_IT_TC, ENABLE);

    SPI_Cmd(SPI1, ENABLE);
	
    if (xLcdEventGroup == NULL) {
        xLcdEventGroup = xEventGroupCreate();
}
}

// 弱存根：阶段 3 LVGL 移植层实现时会被强符号覆盖
__weak void LCD_DMA_TransferComplete(void) {}
// 等待 SPI 完全空闲（仅在切换 DC 信号前使用）
// (既检查 TXE（传输完成标志）又检查 BSY（忙碌标志）, 确保前面的数据彻底发完)
static inline void LCD_Wait_Idle(void) {
    while (!(SPI1->SR & SPI_I2S_FLAG_TXE)){}
    while (SPI1->SR & SPI_I2S_FLAG_BSY){}
}
// 写入16位数据到 LCD（数据模式），并在写入前检查 TXE 标志（不检查 BSY 标志 压榨吞吐量）
static inline void LCD_WR_DATA16(uint16_t data)
{
    while (!(SPI1->SR & SPI_I2S_FLAG_TXE)){} // 仅检查发送缓冲区是否可写
    SPI1->DR = data;
}
// 写入16位寄存器 reg（命令），并在写入前后切换 DC 信号
static inline void LCD_WR_REG16(uint16_t reg)
{
    LCD_Wait_Idle(); // 必须等前面的数据彻底发完，才能拉低 DC 切换为命令模式
    LCD_DC_Low();
    
    SPI1->DR = reg;
    
    LCD_Wait_Idle(); // 等待命令发完，再拉高 DC 恢复数据模式
    LCD_DC_High();
}
// 设置 LCD 显示区域（窗口）为 (x1,y1) 到 (x2,y2)
void LCD_Address_Set(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2)
{
	LCD_WR_REG16((F8T16<<8)|0x2a);//0x2a: 列地址设置
	// 根据屏幕方向调整偏移
	LCD_WR_DATA16(x1 + ((USE_HORIZONTAL == 3) ? 80 : 0));// 根据屏幕方向调整偏移
	LCD_WR_DATA16(x2 + ((USE_HORIZONTAL == 3) ? 80 : 0));// 根据屏幕方向调整偏移
	LCD_WR_REG16((F8T16<<8)|0x2b);//0x2b: 行地址设置
	LCD_WR_DATA16(y1 + ((USE_HORIZONTAL == 1) ? 80 : 0));// 根据屏幕方向调整偏移
	LCD_WR_DATA16(y2 + ((USE_HORIZONTAL == 1) ? 80 : 0));// 根据屏幕方向调整偏移
	LCD_WR_REG16((F8T16<<8)|0x2c);//0x2c: 内存写入
}
// LCD 初始化函数
void LCD_Init(void)
{
	LCD_GPIO_Init();//初始化GPIO
	
	LCD_BLK_Low();//打开背光
	// 保证在复位后等待足够的时间，确保 LCD 控制器完成初始化
	LCD_RES_High();
	LCD_RES_Low();//复位
	Delay_ms(1);
	LCD_RES_High();
	Delay_ms(120);

	LCD_WR_REG16((F8T16<<8)|0x11);//0x11:  退出睡眠模式 
	
    // 设置内存数据访问控制寄存器（MADCTL）以配置屏幕方向
	LCD_WR_REG16((F8T16<<8)|0x36);//0x36: 内存数据访问控制
	if     (USE_HORIZONTAL==0) LCD_WR_DATA16((0x00<<8)|F8T16);//0x00: 竖屏模式
    else if(USE_HORIZONTAL==1) LCD_WR_DATA16((0xC0<<8)|F8T16);//0xC0: 横屏模式
    else if(USE_HORIZONTAL==2) LCD_WR_DATA16((0x70<<8)|F8T16);//0x70: 竖屏翻转模式
    else                       LCD_WR_DATA16((0xA0<<8)|F8T16);//0xA0: 横屏翻转模式
	// 设置接口像素格式为16位/像素
	LCD_WR_REG16((F8T16<<8)|0x3A);//0x3A: 接口像素格式设置     
	LCD_WR_DATA16((0x05<<8)|F8T16);//0x05: 16位/pixel
    // 设置空白 porch、门控电压、VCOMS、电源控制、伽马校正等参数
	LCD_WR_REG16((F8T16<<8)|0xB2);//0xB2: 空白 porch 设置
	LCD_WR_DATA16((0x0C<<8)|0x0C);// 上/下空白 porch
	LCD_WR_DATA16((0x00<<8)|0x33);// 上/下非显示行
	LCD_WR_DATA16((0x33<<8)|F8T16);// 列空白 porch

	LCD_WR_REG16((F8T16<<8)|0xB7);//0xB7: 门控电压调整  
	LCD_WR_DATA16((0x35<<8)|F8T16);// 门控电压设置

	LCD_WR_REG16((F8T16<<8)|0xBB);//0xBB: VCOMS 电压设置     
	LCD_WR_DATA16((0x32<<8)|F8T16);// VCOM 电压值

	LCD_WR_REG16((F8T16<<8)|0xC2);//0xC2: VDVVRHEN 设置     
	LCD_WR_DATA16((0x01<<8)|F8T16);// VDV 和 VRH 使能

	LCD_WR_REG16((F8T16<<8)|0xC3);//0xC3: VRH 设置     
	LCD_WR_DATA16((0x10<<8)|F8T16);// GVDD 电压值

	LCD_WR_REG16((F8T16<<8)|0xC4);//0xC4: VDVS 设置     
	LCD_WR_DATA16((0x20<<8)|F8T16);// VDV 电压值

	LCD_WR_REG16((F8T16<<8)|0xC6);//0xC6: 帧率控制     
	LCD_WR_DATA16((0x0F<<8)|F8T16);// 帧率设置
	
	LCD_WR_REG16((F8T16<<8)|0xD0);//0xD0: 电源控制
	LCD_WR_DATA16((0xA4<<8)|0xA1);// 电源电压设置

	LCD_WR_REG16((F8T16<<8)|0xE0);//0xE0: 正伽马校正曲线
	LCD_WR_DATA16((0xD0<<8)|0x08);
	LCD_WR_DATA16((0x0E<<8)|0x09);
	LCD_WR_DATA16((0x09<<8)|0x05);
	LCD_WR_DATA16((0x31<<8)|0x33);
	LCD_WR_DATA16((0x48<<8)|0x17);
	LCD_WR_DATA16((0x14<<8)|0x15);
	LCD_WR_DATA16((0x31<<8)|0x34);

	LCD_WR_REG16((F8T16<<8)|0xE1);//0xE1: 负伽马校正曲线
	LCD_WR_DATA16((0xD0<<8)|0x08);
	LCD_WR_DATA16((0x0E<<8)|0x09);
	LCD_WR_DATA16((0x09<<8)|0x15);
	LCD_WR_DATA16((0x31<<8)|0x33);
	LCD_WR_DATA16((0x48<<8)|0x17);
	LCD_WR_DATA16((0x14<<8)|0x15);
	LCD_WR_DATA16((0x31<<8)|0x34);

	LCD_WR_REG16((F8T16<<8)|0x21);//0x21: 显示反转开启     

	LCD_WR_REG16((F8T16<<8)|0x29);//0x29: 显示开启
}

// 纯粹的底层 DMA 发送函数（不设地址）data: 16位像素数据起始地址，pixel_count: 像素数量
void LCD_Write_DMA(uint16_t *data, uint32_t pixel_count)
{
    lcd_dma_user = g_lcd_user; //获取本次传输的真正发起者
    DMA2_Stream5->M0AR = (uint32_t)data;// 设置 DMA 内存地址为数据起始地址
    DMA2_Stream5->NDTR = pixel_count;// 设置 DMA 数据传输数量
    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, ENABLE);
    DMA_Cmd(DMA2_Stream5, ENABLE);
}

// 供 LVGL 使用的颜色填充 xsta: 起始 X 坐标, ysta: 起始 Y 坐标, xend: 结束 X 坐标, yend: 结束 Y 坐标, color: 颜色数据数组
void LCD_Color_Fill(uint16_t xsta, uint16_t ysta, uint16_t xend, uint16_t yend, uint16_t *color)
{
    LCD_Address_Set(xsta, ysta, xend, yend);
    uint32_t pixel_count = (xend - xsta + 1) * (yend - ysta + 1);
    LCD_Write_DMA(color, pixel_count);
}
// DMA2 Stream5 中断服务函数（用于处理 LCD DMA 传输完成事件）
void DMA2_Stream5_IRQHandler(void)
{
    if(DMA_GetITStatus(DMA2_Stream5, DMA_IT_TCIF5))
    {
        DMA_ClearITPendingBit(DMA2_Stream5, DMA_IT_TCIF5);
        DMA_Cmd(DMA2_Stream5, DISABLE);
        SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);
        while(SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET);// 等待 SPI 总线空闲

        extern void LCD_DMA_TransferComplete(void);
        if(lcd_dma_user == LCD_USER_LVGL) LCD_DMA_TransferComplete();
		else
		{   // 如果是其他任务发起的 DMA 传输，设置事件组标志位通知该任务传输完成
			BaseType_t xHigherPriorityTaskWoken = pdFALSE;
			xEventGroupSetBitsFromISR(xLcdEventGroup, lcd_dma_user, &xHigherPriorityTaskWoken);// 设置事件组标志位
			portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
		}
    }
}