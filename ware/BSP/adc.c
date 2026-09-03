#include "stm32f4xx.h"
#include "pin_ctrl.h"
#include "variables.h"
#include "defines.h"

// ADC通道数量定义
#define ADC_Channel_Count 8

static volatile uint16_t adc_value[ADC_Channel_Count]; //DMA缓冲区用于存储ADC转换结果 (直接使用这个原始数据) 0：USB_CC2, 1：USB_CC1, 2：VBAT/3, 3：左摇杆X, 4：左摇杆Y, 5：右摇杆X, 6：右摇杆Y, 7：VREFINT

//开启ADC转换
void ADC_StartConversion(void)
{   
    g_adc_dma_finished = 0; // 清除DMA传输完成标志
    DMA_Cmd(DMA2_Stream0, ENABLE); // 启用DMA传输
    ADC_SoftwareStartConv(ADC1);
}
//ADC1和DMA初始化函数
void ADC1_DMA_Init(void)
{
    //GPIO配置
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA | RCC_AHB1Periph_GPIOB | RCC_AHB1Periph_GPIOC , ENABLE);
    
    //PA1-PA3 作为模拟输入 PA1:USB_CC2, PA2:USB_CC1, PA3:VBAT/3
    GPIO_InitStructure.GPIO_Pin =  GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AN;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    //PB0-PB1 作为模拟输入 PB0:左摇杆X, PB1:左摇杆Y
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    //PC0-PC1 作为模拟输入 PC0:右摇杆X, PC1:右摇杆Y
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    //配置DMA
    DMA_InitTypeDef DMA_InitStructure;
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);
    DMA_DeInit(DMA2_Stream0);
    DMA_InitStructure.DMA_Channel = DMA_Channel_0;
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->DR;//ADC数据寄存器地址
    DMA_InitStructure.DMA_Memory0BaseAddr = (uint32_t)adc_value;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralToMemory;
    DMA_InitStructure.DMA_BufferSize = ADC_Channel_Count;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;      //单次模式
    DMA_InitStructure.DMA_Priority = DMA_Priority_Low; //优先级
    DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Disable;
    DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_HalfFull;
    DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_Single;
    DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
    DMA_Init(DMA2_Stream0, &DMA_InitStructure);
    //配置DMA传输完成中断
    DMA_ITConfig(DMA2_Stream0, DMA_IT_TC, ENABLE);
    
    //配置ADC
    // 配置ADC公共参数
    ADC_CommonInitTypeDef ADC_CommonInitStructure;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
    ADC_CommonInitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_CommonInitStructure.ADC_Prescaler = ADC_Prescaler_Div4; //ADC时钟分频
    ADC_CommonInitStructure.ADC_DMAAccessMode = ADC_DMAAccessMode_Disabled;
    ADC_CommonInitStructure.ADC_TwoSamplingDelay = ADC_TwoSamplingDelay_20Cycles;    
    ADC_CommonInit(&ADC_CommonInitStructure);
    // 配置ADC1参数
    ADC_InitTypeDef ADC_InitStructure;
    ADC_InitStructure.ADC_Resolution = ADC_Resolution_12b;
    ADC_InitStructure.ADC_ScanConvMode = ENABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE; //单次转换模式
    ADC_InitStructure.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_None;
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_T1_CC1;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfConversion = ADC_Channel_Count;
    ADC_TempSensorVrefintCmd(ENABLE); //启用内部参考电压
    ADC_Init(ADC1, &ADC_InitStructure);
    // 配置ADC通道及采样顺序
    // PA2 (ADC1_IN2) usb cc1
    ADC_RegularChannelConfig(ADC1, ADC_Channel_2, 1, ADC_SampleTime_480Cycles);
    // PA1 (ADC1_IN1) usb cc2
    ADC_RegularChannelConfig(ADC1, ADC_Channel_1, 2, ADC_SampleTime_480Cycles);
    // PA3 (ADC1_IN3) 2/3vbat
    ADC_RegularChannelConfig(ADC1, ADC_Channel_3, 3, ADC_SampleTime_480Cycles);
    // PB0 (ADC1_IN8) 左摇杆X
    ADC_RegularChannelConfig(ADC1, ADC_Channel_8, 4, ADC_SampleTime_480Cycles);
    // PB1 (ADC1_IN9) 左摇杆Y
    ADC_RegularChannelConfig(ADC1, ADC_Channel_9, 5, ADC_SampleTime_480Cycles);
    // PC0 (ADC1_IN10) 右摇杆X
    ADC_RegularChannelConfig(ADC1, ADC_Channel_10, 6, ADC_SampleTime_480Cycles);
    // PC1 (ADC1_IN11) 右摇杆Y
    ADC_RegularChannelConfig(ADC1, ADC_Channel_11, 7, ADC_SampleTime_480Cycles);
    // 内部参考电压 (ADC1 Ch17)
    ADC_RegularChannelConfig(ADC1, ADC_Channel_17, 8, ADC_SampleTime_480Cycles);
    // 使能ADC DMA请求
    ADC_DMARequestAfterLastTransferCmd(ADC1, ENABLE); 
    // 使能ADC DMA传输
    ADC_DMACmd(ADC1, ENABLE);
    // 启动一次ADC转换
    ADC_StartConversion(); 
}

// 基于12位ADC (0-4095, 3.3V基准) 的阈值区间
#define ADC_THR_RA_MAX       250   // 判定 Ra (线缆芯片) 的上限 250/4095 * 3.3V ≈ 0.2V
#define ADC_THR_RD_DEF_MAX   820   // 判定 默认USB供电 (0.66V) 的上限 820/4095 * 3.3V ≈ 0.66V
#define ADC_THR_RD_1A5_MAX   1526  // 判定 1.5A供电 (1.23V) 的上限    1526/4095 * 3.3V ≈ 1.23V
#define ADC_THR_RD_3A_MAX    2600  // 判定 3.0A供电 (最高2.04V) 的上限  2600/4095 * 3.3V ≈ 2.04V
#define ADC_THR_OPEN_MIN     3500  // 判定 开路/悬空状态的下限  3500/4095 * 3.3V ≈ 2.88V

// 摇杆限幅及死区处理 (原地修改输入值)
static inline void limit_and_deadzone(int16_t *val)
{
    if (*val > 127) *val = 127;
    else if (*val < -128) *val = -128;
    if (*val < 20 && *val > -20) *val = 0;
}

// 获取Type-C状态
void Get_TypeC_Status(void)
{
	static uint8_t cc_host_count = 0;
	uint8_t last_count = cc_host_count;
	//获得当前的CC电压值（判断正反插）
	uint16_t slav_cc_max = (g_slave_cc1_value > g_slave_cc2_value) ? g_slave_cc1_value : g_slave_cc2_value;
	uint16_t host_cc_max = (g_host_cc1_value > g_host_cc2_value) ? g_host_cc1_value : g_host_cc2_value;
	uint16_t host_cc_min = (g_host_cc1_value < g_host_cc2_value) ? g_host_cc1_value : g_host_cc2_value;
	
	if(host_cc_max < ADC_THR_RD_DEF_MAX){
		// 主机侧两脚都 < 0.66V → 本机未作为主机连接, 检查设备侧是否有Ra(线缆芯片)
		if(slav_cc_max > ADC_THR_RA_MAX) 			g_usb_status = TYPEC_CC_OKEY; // 设备侧有Ra → CtoC设备模式
		else 								 			cc_host_count++;             // 设备侧无Ra → 可能悬空, 计数等待确认
	}
	else{
		// 主机侧至少一脚 >= 0.66V → 本机可能作为主机
		if(host_cc_min < ADC_THR_RA_MAX){
			// 主机侧一脚悬空(<0.2V), 另一脚有电压 → 正反插导致一脚未连接
			if(slav_cc_max > ADC_THR_RA_MAX) 		    g_usb_status = TYPEC_CC_OKEY; // 设备侧有Ra → CtoC设备模式
			else 								 		g_usb_status = TYPEC_CC_IDLE; // 设备侧无Ra → CtoC线缆空闲(未建立连接)
		}
		else if(host_cc_min < ADC_THR_RD_DEF_MAX) 		g_usb_status = TYPEC_IS_HOST; // 主机侧两脚都在0.2~0.66V → 直接主机模式
		else if(host_cc_min < ADC_THR_OPEN_MIN) 		g_usb_status = TYPEC_AC_IDLE; // 主机侧两脚在0.66~2.8V → AtoC线缆空闲
		else{
			// 主机侧两脚都 > 2.8V → 开路/悬空, 检查设备侧
			if(slav_cc_max > ADC_THR_RA_MAX) 			g_usb_status = TYPEC_AC_OKEY; // 设备侧有Ra → AtoC设备模式
			else 								 		g_usb_status = TYPEC_NO_FIND; // 设备侧也无Ra → 完全未连接
		}
	}
	if(cc_host_count > 9)  								g_usb_status = TYPEC_CC_HOST;// 设备侧无Ra, 且主机侧两脚都 < 0.2V → CtoC主机模式
	if(last_count == cc_host_count || cc_host_count>9)	cc_host_count = 0;// 如果计数器未增加或超过9次, 则重置计数器
	// 根据当前状态控制USB_Slave_Host_Ctrl函数
	if(g_usb_status == TYPEC_AC_OKEY || g_usb_status == TYPEC_CC_OKEY) USB_Slave_Host_Ctrl(0);
	if(g_usb_status == TYPEC_CC_HOST || g_usb_status == TYPEC_IS_HOST) USB_Slave_Host_Ctrl(1);
}

// ADC计算电压值
void ADC_Calculate_Voltage(void)
{
    // 读取内部参考电压校准值 (出厂校准值)
    float Vrefint = *(__IO uint16_t *)(0x1FFF7A2A);
    
    // 提取公共系数，减少浮点乘除法次数
    // 注意：这里全部改为直接使用原始的 adc_value
    float common_voltage_factor = (Vrefint * 0.0008056640625f) / adc_value[7];
    float vdda_real_factor = Vrefint / adc_value[7]; // 摇杆使用的系数

    float temp_bat_voltage;
    int16_t temp_LX, temp_LY, temp_RX, temp_RY;
	
	//cc计算逻辑
	static uint8_t state = 0;
	state = (state+1)%5;
	
	if(g_usb_status < 3)
	{
		switch(state)//0 - 4
		{
			case 0: 
				USB_Slave_Host_Ctrl(0); 
				break;
			case 2: 
				g_slave_cc1_value = adc_value[0]; 
				g_slave_cc2_value = adc_value[1]; 
				USB_Slave_Host_Ctrl(1);
				break;
			case 4: 
				g_host_cc1_value = adc_value[0]; 
				g_host_cc2_value = adc_value[1];
				Get_TypeC_Status(); 
				break;
			default: 
				break;
		}
	}
	else
	{
		static uint8_t disconnect_cnt = 0; 
		uint8_t is_disconnect = 0;
		
		if(g_usb_status > 4) //host
		{
			g_host_cc1_value = adc_value[0]; g_host_cc2_value = adc_value[1];
			uint16_t max = (adc_value[0] > adc_value[1]) ? adc_value[0] : adc_value[1];
			uint16_t min = (adc_value[0] < adc_value[1]) ? adc_value[0] : adc_value[1];
			if(g_usb_status == TYPEC_CC_HOST && max > ADC_THR_OPEN_MIN)  is_disconnect = 1;
			if(g_usb_status == TYPEC_IS_HOST && min > ADC_THR_OPEN_MIN)  is_disconnect = 1;
		}
		else                 //device
		{
			g_slave_cc1_value = adc_value[0]; g_slave_cc2_value = adc_value[1];
			uint16_t max = (adc_value[0] > adc_value[1]) ? adc_value[0] : adc_value[1];
			if(max < ADC_THR_RA_MAX)        							 is_disconnect = 1;
		}
		
		if(is_disconnect)
		{
			disconnect_cnt++;
			if(disconnect_cnt > 9)
			{
				g_usb_status = TYPEC_NO_FIND; 
				USB_Slave_Host_Ctrl(0);
				disconnect_cnt = 0;
				state = 4;
			}
		}
		else disconnect_cnt = 0; 
	}
	
    // 计算电池电压
    temp_bat_voltage = adc_value[2] * (common_voltage_factor * 1.5f);

    // 电池电压微小波动过滤 (迟滞)
    if (temp_bat_voltage > (g_battery_voltage + 0.01f) || temp_bat_voltage < (g_battery_voltage - 0.01f))
    {
        g_battery_voltage = temp_bat_voltage;
    }
    
    // 摇杆计算
    temp_LX = (int16_t)((adc_value[3] * vdda_real_factor - 2048) / 8.0f);
    temp_LY = (int16_t)((adc_value[4] * vdda_real_factor - 2048) / 8.0f);
    temp_RX = (int16_t)((adc_value[5] * vdda_real_factor - 2048) / 8.0f);
    temp_RY = (int16_t)((2048 - adc_value[6] * vdda_real_factor) / 8.0f);
    
    // 摇杆限幅及死区处理
    limit_and_deadzone(&temp_LX);
    limit_and_deadzone(&temp_LY);
    limit_and_deadzone(&temp_RX);
    limit_and_deadzone(&temp_RY);
    
    // 更新全局摇杆值 (减少写入操作)
    if (temp_LX != g_key_L_X) g_key_L_X = temp_LX;
    if (temp_LY != g_key_L_Y) g_key_L_Y = temp_LY;
    if (temp_RX != g_key_R_X) g_key_R_X = temp_RX;
    if (temp_RY != g_key_R_Y) g_key_R_Y = temp_RY;
}

// DMA2 Stream0中断服务函数
void DMA2_Stream0_IRQHandler(void) 
{
    if (DMA_GetITStatus(DMA2_Stream0, DMA_IT_TCIF0)) 
    {
        g_adc_dma_finished = 1;
        DMA_Cmd(DMA2_Stream0, DISABLE);
        DMA_ClearITPendingBit(DMA2_Stream0, DMA_IT_TCIF0);
    }
}