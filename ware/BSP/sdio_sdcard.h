#ifndef __SDIO_SDCARD_H__
#define __SDIO_SDCARD_H__																			   
		
#include "stm32f4xx.h" 

//Sdio相关标志位,拷贝自:stm32f4xx sdio.h
#define SDIO_FLAG_CCRCFAIL                  ((uint32_t)0x00000001)
#define SDIO_FLAG_DCRCFAIL                  ((uint32_t)0x00000002)
#define SDIO_FLAG_CTIMEOUT                  ((uint32_t)0x00000004)
#define SDIO_FLAG_DTIMEOUT                  ((uint32_t)0x00000008)
#define SDIO_FLAG_TXUNDERR                  ((uint32_t)0x00000010)
#define SDIO_FLAG_RXOVERR                   ((uint32_t)0x00000020)
#define SDIO_FLAG_CMDREND                   ((uint32_t)0x00000040)
#define SDIO_FLAG_CMDSENT                   ((uint32_t)0x00000080)
#define SDIO_FLAG_DATAEND                   ((uint32_t)0x00000100)
#define SDIO_FLAG_STBITERR                  ((uint32_t)0x00000200)
#define SDIO_FLAG_DBCKEND                   ((uint32_t)0x00000400)
#define SDIO_FLAG_CMDACT                    ((uint32_t)0x00000800)
#define SDIO_FLAG_TXACT                     ((uint32_t)0x00001000)
#define SDIO_FLAG_RXACT                     ((uint32_t)0x00002000)
#define SDIO_FLAG_TXFIFOHE                  ((uint32_t)0x00004000)
#define SDIO_FLAG_RXFIFOHF                  ((uint32_t)0x00008000)
#define SDIO_FLAG_TXFIFOF                   ((uint32_t)0x00010000)
#define SDIO_FLAG_RXFIFOF                   ((uint32_t)0x00020000)
#define SDIO_FLAG_TXFIFOE                   ((uint32_t)0x00040000)
#define SDIO_FLAG_RXFIFOE                   ((uint32_t)0x00080000)
#define SDIO_FLAG_TXDAVL                    ((uint32_t)0x00100000)
#define SDIO_FLAG_RXDAVL                    ((uint32_t)0x00200000)
#define SDIO_FLAG_SDIOIT                    ((uint32_t)0x00400000)
#define SDIO_FLAG_CEATAEND                  ((uint32_t)0x00800000)

//用户配置区			  
//SDIO时钟计算公式:SDIO_CK时钟=SDIOCLK/[clkdiv+2];其中,SDIOCLK固定为48Mhz
//使用DMA模式的时候,传输速率可以到48Mhz(bypass on时),不过如果你的卡不是高速
//卡,可能也会出错,出错就请降低时钟
#define SDIO_INIT_CLK_DIV        0x76 		//Sdio初始化频率，最大400 khz 
#define SDIO_TRANSFER_CLK_DIV    0x00		//Sdio传输频率,该值太小可能会导致读写文件出错 
										 
//Sdio工作模式定义,通过sd set device mode函数设置.
#define SD_POLLING_MODE    	0  	//查询模式,该模式下,如果读写有问题,建议增大sdio transfer clk div的设置.
#define SD_DMA_MODE    		1	//Dma模式,该模式下,如果读写有问题,建议增大sdio transfer clk div的设置.   

//SDIO 各种错误枚举定义
typedef enum
{
	SD_OK                              = (0), 
	SD_NO_CARD                         = (1),
	//标准错误定义
	SD_INTERNAL_ERROR                  = (2), 
	SD_NOT_CONFIGURED                  = (3),  
	SD_REQUEST_PENDING                 = (4),  
	SD_REQUEST_NOT_APPLICABLE          = (5), 
	SD_INVALID_PARAMETER               = (6),  
	SD_UNSUPPORTED_FEATURE             = (7),  
	SD_UNSUPPORTED_HW                  = (8),  
	SD_ERROR                           = (9),  
	//特殊错误定义 
	SD_CMD_CRC_FAIL                    = (10), /*！<收到命令响应（但CRC校验失败） */
	SD_DATA_CRC_FAIL                   = (11), /*！<发送/接收数据块（CRC校验失败） */
	SD_CMD_RSP_TIMEOUT                 = (12), /*！<命令响应超时 */
	SD_DATA_TIMEOUT                    = (13), /*！<数据超时 */
	SD_TX_UNDERRUN                     = (14), /*！<正在运行时传输FIFO */
	SD_RX_OVERRUN                      = (15), /*！<接收FIFO超限 */
	SD_START_BIT_ERR                   = (16), /*!< Start bit not detected on all data signals in widE bus mode */
	SD_CMD_OUT_OF_RANGE                = (17), /*！<CMD的参数超出范围。*/
	SD_ADDR_MISALIGNED                 = (18), /*!< Misaligned address */
	SD_BLOCK_LEN_ERR                   = (19), /*!< Transferred block length is not allowed for the card or the number of transferred bytes does not match the block length */
	SD_ERASE_SEQ_ERR                   = (20), /*!< An error in the sequence of erase command occurs.*/
	SD_BAD_ERASE_PARAM                 = (21), /*！<擦除组的选择无效 */
	SD_WRITE_PROT_VIOLATION            = (22), /*！<尝试编写写保护块 */
	SD_LOCK_UNLOCK_FAILED              = (23), /*！<在解锁命令中检测到序列或密码错误，或者是否有人试图访问锁定的卡 */
	SD_COM_CRC_FAILED                  = (24), /*!< CRC check of the previous command failed */
	SD_ILLEGAL_CMD                     = (25), /*!< Command is not legal for the card state */
	SD_CARD_ECC_FAILED                 = (26), /*！<应用了卡内部ECC，但无法纠正数据 */
	SD_CC_ERROR                        = (27), /*!< Internal card controller error */
	SD_GENERAL_UNKNOWN_ERROR           = (28), /*!< General or Unknown error */
	SD_STREAM_READ_UNDERRUN            = (29), /*!< The card could not sustain data transfer in stream read operation. */
	SD_STREAM_WRITE_OVERRUN            = (30), /*!< The card could not sustain data programming in stream mode */
	SD_CID_CSD_OVERWRITE               = (31), /*！<CID/CSD覆盖错误 */
	SD_WP_ERASE_SKIP                   = (32), /*！<仅部分地址空间被擦除 */
	SD_CARD_ECC_DISABLED               = (33), /*！<未使用内部ECC执行命令 */
	SD_ERASE_RESET                     = (34), /*!< Erase sequence was cleared before executing because an out of erase sequence command was received */
	SD_AKE_SEQ_ERROR                   = (35), /*！<身份验证顺序错误。 */
	SD_INVALID_VOLTRANGE               = (36),
	SD_ADDR_OUT_OF_RANGE               = (37),
	SD_SWITCH_ERROR                    = (38),
	SD_SDIO_DISABLED                   = (39),
	SD_SDIO_FUNCTION_BUSY              = (40),
	SD_SDIO_FUNCTION_FAILED            = (41),
	SD_SDIO_UNKNOWN_FUNCTION           = (42),
} SD_Error;		  

//Sd卡csd寄存器数据		  
typedef struct
{
	uint8_t  CSDStruct;            /*！<CSD结构 */
	uint8_t  SysSpecVersion;       /*！<系统规范版本 */
	uint8_t  Reserved1;            /*!< Reserved */
	uint8_t  TAAC;                 /*！<数据读取访问时间1 */
	uint8_t  NSAC;                 /*！<数据读取访问时间2（CLK周期） */
	uint8_t  MaxBusClkFrec;        /*!< Max. bus clock frequency */
	uint16_t CardComdClasses;      /*！<卡命令类 */
	uint8_t  RdBlockLen;           /*！<最大读取数据块长度 */
	uint8_t  PartBlockRead;        /*！<允许读取部分块 */
	uint8_t  WrBlockMisalign;      /*！<写入块错位 */
	uint8_t  RdBlockMisalign;      /*！<读取块错位 */
	uint8_t  DSRImpl;              /*！<DSR已实现 */
	uint8_t  Reserved2;            /*!< Reserved */
	uint32_t DeviceSize;           /*！<设备大小 */
	uint8_t  MaxRdCurrentVDDMin;   /*！<VDD最小时的最大读取电流 */
	uint8_t  MaxRdCurrentVDDMax;   /*！<VDD最大时的最大读取电流 */
	uint8_t  MaxWrCurrentVDDMin;   /*!< Max. write current @ VDD min */
	uint8_t  MaxWrCurrentVDDMax;   /*!< Max. write current @ VDD max */
	uint8_t  DeviceSizeMul;        /*!< Device size multiplier */
	uint8_t  EraseGrSize;          /*!< Erase group size */
	uint8_t  EraseGrMul;           /*！<擦除组大小倍数 */
	uint8_t  WrProtectGrSize;      /*！<写保护组大小 */
	uint8_t  WrProtectGrEnable;    /*!< Write protect group enable */
	uint8_t  ManDeflECC;           /*!< Manufacturer default ECC */
	uint8_t  WrSpeedFact;          /*!< Write speed factor */
	uint8_t  MaxWrBlockLen;        /*！<最大写入数据块长度 */
	uint8_t  WriteBlockPaPartial;  /*！<允许写入部分块 */
	uint8_t  Reserved3;            /*！<研究 */
	uint8_t  ContentProtectAppli;  /*！<内容保护应用程序 */
	uint8_t  FileFormatGrouop;     /*!< File format group */
	uint8_t  CopyFlag;             /*!< Copy flag (OTP) */
	uint8_t  PermWrProtect;        /*！<永久写保护 */
	uint8_t  TempWrProtect;        /*！<临时写保护 */
	uint8_t  FileFormat;           /*！<文件格式 */
	uint8_t  ECC;                  /*！<ECC码 */
	uint8_t  CSD_CRC;              /*!< CSD CRC */
	uint8_t  Reserved4;            /*!< always 1*/
} SD_CSD;   

//SD卡CID寄存器数据
typedef struct
{
	uint8_t  ManufacturerID;       /*!< ManufacturerID */
	uint16_t OEM_AppliID;          /*!< OEM/Application ID */
	uint32_t ProdName1;            /*！<产品名称第1部分 */
	uint8_t  ProdName2;            /*!< Product Name part2*/
	uint8_t  ProdRev;              /*!< Product Revision */
	uint32_t ProdSN;               /*！<产品序列号 */
	uint8_t  Reserved1;            /*！<已预订1 */
	uint16_t ManufactDate;         /*!< Manufacturing Date */
	uint8_t  CID_CRC;              /*!< CID CRC */
	uint8_t  Reserved2;            /*!< always 1 */
} SD_CID;	

//SD卡状态
typedef enum
{
	SD_CARD_READY                  = ((uint32_t)0x00000001),
	SD_CARD_IDENTIFICATION         = ((uint32_t)0x00000002),
	SD_CARD_STANDBY                = ((uint32_t)0x00000003),
	SD_CARD_TRANSFER               = ((uint32_t)0x00000004),
	SD_CARD_SENDING                = ((uint32_t)0x00000005),
	SD_CARD_RECEIVING              = ((uint32_t)0x00000006),
	SD_CARD_PROGRAMMING            = ((uint32_t)0x00000007),
	SD_CARD_DISCONNECTED           = ((uint32_t)0x00000008),
	SD_CARD_ERROR                  = ((uint32_t)0x000000FF)
}SDCardState;

//Sd卡信息,包括csd,cid等数据
typedef struct
{
	SD_CSD SD_csd;
	SD_CID SD_cid;
	long long CardCapacity;  	    //SD卡容量,单位:字节,最大支持2^64字节大小的卡.
	uint32_t CardBlockSize; 		//Sd卡块大小	
	uint16_t RCA;					//卡相对地址
	uint8_t CardType;				//卡类型
} SD_CardInfo;
extern SD_CardInfo SDCardInfo;//Sd卡信息			 

//SDIO 指令集
//基本命令 (CMD0~CMD64)
#define SD_CMD_GO_IDLE_STATE                       ((uint8_t)0)      //CMD0: 复位SD卡，进入空闲状态
#define SD_CMD_SEND_OP_COND                        ((uint8_t)1)      //CMD1: 发送操作条件（MMC卡使用）
#define SD_CMD_ALL_SEND_CID                        ((uint8_t)2)      //CMD2: 获取CID（卡识别数据）
#define SD_CMD_SET_REL_ADDR                        ((uint8_t)3)      //CMD3: 设置相对地址RCA
#define SD_CMD_SET_DSR                             ((uint8_t)4)      //CMD4: 设置驱动阶段
#define SD_CMD_SDIO_SEN_OP_COND                    ((uint8_t)5)      //CMD5: SDIO发送操作条件
#define SD_CMD_HS_SWITCH                           ((uint8_t)6)      //CMD6: 切换模式（高速/宽总线）
#define SD_CMD_SEL_DESEL_CARD                      ((uint8_t)7)      //CMD7: 选中/取消选中卡
#define SD_CMD_HS_SEND_EXT_CSD                     ((uint8_t)8)      //CMD8: 发送扩展CSD
#define SD_CMD_SEND_CSD                            ((uint8_t)9)      //CMD9: 获取CSD（卡特定数据）
#define SD_CMD_SEND_CID                            ((uint8_t)10)     //CMD10: 获取CID
#define SD_CMD_READ_DAT_UNTIL_STOP                 ((uint8_t)11)     //CMD11: 读数据直到停止（SD卡不支持）
#define SD_CMD_STOP_TRANSMISSION                   ((uint8_t)12)     //CMD12: 停止传输
#define SD_CMD_SEND_STATUS                         ((uint8_t)13)     //CMD13: 发送状态查询
#define SD_CMD_HS_BUSTEST_READ                     ((uint8_t)14)     //CMD14: 总线测试读
#define SD_CMD_GO_INACTIVE_STATE                   ((uint8_t)15)     //CMD15: 进入非活动状态
#define SD_CMD_SET_BLOCKLEN                        ((uint8_t)16)     //CMD16: 设置块长度
#define SD_CMD_READ_SINGLE_BLOCK                   ((uint8_t)17)     //CMD17: 读单个块
#define SD_CMD_READ_MULT_BLOCK                     ((uint8_t)18)     //CMD18: 读多个块
#define SD_CMD_HS_BUSTEST_WRITE                    ((uint8_t)19)     //CMD19: 总线测试写
#define SD_CMD_WRITE_DAT_UNTIL_STOP                ((uint8_t)20)     //CMD20: 写数据直到停止
#define SD_CMD_SET_BLOCK_COUNT                     ((uint8_t)23)     //CMD23: 设置块数量
#define SD_CMD_WRITE_SINGLE_BLOCK                  ((uint8_t)24)     //CMD24: 写单个块
#define SD_CMD_WRITE_MULT_BLOCK                    ((uint8_t)25)     //CMD25: 写多个块
#define SD_CMD_PROG_CID                            ((uint8_t)26)     //CMD26: 编程CID
#define SD_CMD_PROG_CSD                            ((uint8_t)27)     //CMD27: 编程CSD
#define SD_CMD_SET_WRITE_PROT                      ((uint8_t)28)     //CMD28: 设置写保护
#define SD_CMD_CLR_WRITE_PROT                      ((uint8_t)29)     //CMD29: 清除写保护
#define SD_CMD_SEND_WRITE_PROT                     ((uint8_t)30)     //CMD30: 发送写保护状态
#define SD_CMD_SD_ERASE_GRP_START                  ((uint8_t)32)     //CMD32: 设置擦除组起始地址（SD卡）
#define SD_CMD_SD_ERASE_GRP_END                    ((uint8_t)33)     //CMD33: 设置擦除组结束地址（SD卡）
#define SD_CMD_ERASE_GRP_START                     ((uint8_t)35)     //CMD35: 设置擦除组起始地址（MMC卡）
#define SD_CMD_ERASE_GRP_END                       ((uint8_t)36)     //CMD36: 设置擦除组结束地址（MMC卡）
#define SD_CMD_ERASE                               ((uint8_t)38)     //CMD38: 擦除
#define SD_CMD_FAST_IO                             ((uint8_t)39)     //CMD39: 快速IO（SD卡不支持）
#define SD_CMD_GO_IRQ_STATE                        ((uint8_t)40)     //CMD40: 进入中断状态（SD卡不支持）
#define SD_CMD_LOCK_UNLOCK                         ((uint8_t)42)     //CMD42: 锁定/解锁
#define SD_CMD_APP_CMD                             ((uint8_t)55)     //CMD55: 应用特定命令前缀（其后跟ACMD）
#define SD_CMD_GEN_CMD                             ((uint8_t)56)     //CMD56: 通用命令
#define SD_CMD_NO_CMD                              ((uint8_t)64)     //无命令

/** 
  * @brief Following commands are SD Card Specific commands.
  *        SDIO_APP_CMD ：CMD55 should be sent before sending these commands. 
  *        应用特定命令(ACMD)：发送前必须先发CMD55表示这是应用命令
  */
#define SD_CMD_APP_SD_SET_BUSWIDTH                 ((uint8_t)6)      //ACMD6: 设置总线宽度
#define SD_CMD_SD_APP_STAUS                        ((uint8_t)13)     //ACMD13: SD状态
#define SD_CMD_SD_APP_SEND_NUM_WRITE_BLOCKS        ((uint8_t)22)     //ACMD22: 发送已写块数量
#define SD_CMD_SD_APP_OP_COND                      ((uint8_t)41)     //ACMD41: 发送操作条件（SD卡使用）
#define SD_CMD_SD_APP_SET_CLR_CARD_DETECT          ((uint8_t)42)     //ACMD42: 设置/清除卡检测
#define SD_CMD_SD_APP_SEND_SCR                     ((uint8_t)51)     //ACMD51: 发送SCR(SD配置寄存器)
#define SD_CMD_SDIO_RW_DIRECT                      ((uint8_t)52)     //CMD52: SDIO直接读写
#define SD_CMD_SDIO_RW_EXTENDED                    ((uint8_t)53)     //CMD53: SDIO扩展读写

/** 
  * @brief Following commands are SD Card Specific security commands.
  *        SDIO_APP_CMD should be sent before sending these commands. 
  *        安全命令：发送前必须先发CMD55
  */
#define SD_CMD_SD_APP_GET_MKB                      ((uint8_t)43)     //ACMD43: 获取MKB
#define SD_CMD_SD_APP_GET_MID                      ((uint8_t)44)     //ACMD44: 获取MID
#define SD_CMD_SD_APP_SET_CER_RN1                  ((uint8_t)45)     //ACMD45: 设置CER RN1
#define SD_CMD_SD_APP_GET_CER_RN2                  ((uint8_t)46)     //ACMD46: 获取CER RN2
#define SD_CMD_SD_APP_SET_CER_RES2                 ((uint8_t)47)     //ACMD47: 设置CER RES2
#define SD_CMD_SD_APP_GET_CER_RES1                 ((uint8_t)48)     //ACMD48: 获取CER RES1
#define SD_CMD_SD_APP_SECURE_READ_MULTIPLE_BLOCK   ((uint8_t)18)     //ACMD18: 安全读多块
#define SD_CMD_SD_APP_SECURE_WRITE_MULTIPLE_BLOCK  ((uint8_t)25)     //ACMD25: 安全写多块
#define SD_CMD_SD_APP_SECURE_ERASE                 ((uint8_t)38)     //ACMD38: 安全擦除
#define SD_CMD_SD_APP_CHANGE_SECURE_AREA           ((uint8_t)49)     //ACMD49: 改变安全区域
#define SD_CMD_SD_APP_SECURE_WRITE_MKB             ((uint8_t)48)     //ACMD48: 安全写MKB
  			   
//支持的SD卡类型定义
#define SDIO_STD_CAPACITY_SD_CARD_V1_1             ((uint32_t)0x00000000)     //标准容量SD卡 V1.1版本 (<2GB)
#define SDIO_STD_CAPACITY_SD_CARD_V2_0             ((uint32_t)0x00000001)     //标准容量SD卡 V2.0版本 (<2GB)
#define SDIO_HIGH_CAPACITY_SD_CARD                 ((uint32_t)0x00000002)     //高容量SD卡 (SDHC, >=2GB)
#define SDIO_MULTIMEDIA_CARD                       ((uint32_t)0x00000003)     //多媒体卡 MMC
#define SDIO_SECURE_DIGITAL_IO_CARD                ((uint32_t)0x00000004)     //SDIO卡 (仅IO功能)
#define SDIO_HIGH_SPEED_MULTIMEDIA_CARD            ((uint32_t)0x00000005)     //高速mmc卡
#define SDIO_SECURE_DIGITAL_IO_COMBO_CARD          ((uint32_t)0x00000006)     //SDIO+存储器组合卡
#define SDIO_HIGH_CAPACITY_MMC_CARD                ((uint32_t)0x00000007)     //高容量mmc卡

//SDIO相关参数定义
#ifndef NULL                                                                  //空指针
#define NULL 0                                                                   //空指针
#endif
#define SDIO_STATIC_FLAGS               ((uint32_t)0x000005FF)                   //SDIO静态标志位掩码
#define SDIO_CMD0TIMEOUT                ((uint32_t)0x00010000)                   //CMD0命令超时计数
#define SDIO_DATATIMEOUT                ((uint32_t)0xFFFFFFFF)                   //数据超时时间
#define SDIO_FIFO_Address               ((uint32_t)0x40018080)                   //SDIO FIFO寄存器地址

//OCR寄存器错误掩码 - 卡状态R1响应中的错误位
#define SD_OCR_ADDR_OUT_OF_RANGE        ((uint32_t)0x80000000)                   //地址超出范围
#define SD_OCR_ADDR_MISALIGNED          ((uint32_t)0x40000000)                   //地址不对齐
#define SD_OCR_BLOCK_LEN_ERR            ((uint32_t)0x20000000)                   //块长度错误
#define SD_OCR_ERASE_SEQ_ERR            ((uint32_t)0x10000000)                   //擦除顺序错误
#define SD_OCR_BAD_ERASE_PARAM          ((uint32_t)0x08000000)                   //无效擦除参数
#define SD_OCR_WRITE_PROT_VIOLATION     ((uint32_t)0x04000000)                   //写保护违规
#define SD_OCR_LOCK_UNLOCK_FAILED       ((uint32_t)0x01000000)                   //锁定/解锁失败
#define SD_OCR_COM_CRC_FAILED           ((uint32_t)0x00800000)                   //前一个命令CRC校验失败
#define SD_OCR_ILLEGAL_CMD              ((uint32_t)0x00400000)                   //非法命令
#define SD_OCR_CARD_ECC_FAILED          ((uint32_t)0x00200000)                   //卡内部ECC校正失败
#define SD_OCR_CC_ERROR                 ((uint32_t)0x00100000)                   //内部控制器错误
#define SD_OCR_GENERAL_UNKNOWN_ERROR    ((uint32_t)0x00080000)                   //一般未知错误
#define SD_OCR_STREAM_READ_UNDERRUN     ((uint32_t)0x00040000)                   //流读欠载
#define SD_OCR_STREAM_WRITE_OVERRUN     ((uint32_t)0x00020000)                   //流写过载
#define SD_OCR_CID_CSD_OVERWRIETE       ((uint32_t)0x00010000)                   //CID/CSD覆盖错误
#define SD_OCR_WP_ERASE_SKIP            ((uint32_t)0x00008000)                   //写保护，仅部分擦除
#define SD_OCR_CARD_ECC_DISABLED        ((uint32_t)0x00004000)                   //卡ECC被禁用
#define SD_OCR_ERASE_RESET              ((uint32_t)0x00002000)                   //擦除序列被清除
#define SD_OCR_AKE_SEQ_ERROR            ((uint32_t)0x00000008)                   //认证序列错误
#define SD_OCR_ERRORBITS                ((uint32_t)0xFDFFE008)                   //所有错误位组合掩码

//R6响应掩码 - CMD3(设置相对地址)的响应错误位
#define SD_R6_GENERAL_UNKNOWN_ERROR     ((uint32_t)0x00002000)                   //R6: 一般未知错误
#define SD_R6_ILLEGAL_CMD               ((uint32_t)0x00004000)                   //R6: 非法命令
#define SD_R6_COM_CRC_FAILED            ((uint32_t)0x00008000)                   //R6: CRC校验失败

//电压窗口和容量定义
#define SD_VOLTAGE_WINDOW_SD            ((uint32_t)0x80100000)                   //SD卡支持的电压窗口(2.7~3.6V)
#define SD_HIGH_CAPACITY                ((uint32_t)0x40000000)                   //高容量卡标志位HCS(ACMD41)
#define SD_STD_CAPACITY                 ((uint32_t)0x00000000)                   //标准容量卡
#define SD_CHECK_PATTERN                ((uint32_t)0x000001AA)                   //CMD8检测模式(用于识别SD V2.0)
#define SD_VOLTAGE_WINDOW_MMC           ((uint32_t)0x80FF8000)                   //MMC卡支持的电压窗口

//通用常量定义
#define SD_MAX_VOLT_TRIAL               ((uint32_t)0x0000FFFF)                   //电压检测最大尝试次数
#define SD_ALLZERO                      ((uint32_t)0x00000000)                   //全零值

//总线宽度支持标志
#define SD_WIDE_BUS_SUPPORT             ((uint32_t)0x00040000)                   //SCR中: 支持4位宽总线
#define SD_SINGLE_BUS_SUPPORT           ((uint32_t)0x00010000)                   //SCR中: 仅支持1位总线
#define SD_CARD_LOCKED                  ((uint32_t)0x02000000)                   //卡已锁定(cm d13状态)
#define SD_CARD_PROGRAMMING             ((uint32_t)0x00000007)                   //卡正在编程状态
#define SD_CARD_RECEIVING               ((uint32_t)0x00000006)                   //卡正在接收数据状态
#define SD_DATATIMEOUT                  ((uint32_t)0xFFFFFFFF)                   //数据超时
#define SD_0TO7BITS                     ((uint32_t)0x000000FF)                   //位0-7掩码
#define SD_8TO15BITS                    ((uint32_t)0x0000FF00)                   //位8-15掩码
#define SD_16TO23BITS                   ((uint32_t)0x00FF0000)                   //位16 23掩码
#define SD_24TO31BITS                   ((uint32_t)0xFF000000)                   //位24-31掩码
#define SD_MAX_DATA_LENGTH              ((uint32_t)0x01FFFFFF)                   //最大数据长度

//Fifo相关定义
#define SD_HALFFIFO                     ((uint32_t)0x00000008)                   //半fifo(32位/4字节)
#define SD_HALFFIFOBYTES                ((uint32_t)0x00000020)                   //半FIFO字节数(32字节)

//支持的命令类标志
#define SD_CCCC_LOCK_UNLOCK             ((uint32_t)0x00000080)                   //锁定/解锁命令类
#define SD_CCCC_WRITE_PROT              ((uint32_t)0x00000040)                   //写保护命令类
#define SD_CCCC_ERASE                   ((uint32_t)0x00000020)                   //擦除命令类
																	 
//CMD8指令
#define SDIO_SEND_IF_COND               ((uint32_t)0x00000008)                   //CMD8: 发送接口条件

//相关函数定义
SD_Error SD_Init(void);
SD_Error SD_Deinit(void);
void SDIO_Clock_Set(uint8_t clkdiv);

SD_Error SD_PowerON(void);    
SD_Error SD_PowerOFF(void);
SD_Error SD_InitializeCards(void);
SD_Error SD_GetCardInfo(SD_CardInfo *cardinfo);		  
SD_Error SD_EnableWideBusOperation(uint32_t wmode);
SD_Error SD_SetDeviceMode(uint32_t mode);
SD_Error SD_SelectDeselect(uint32_t addr); 
SD_Error SD_SendStatus(uint32_t *pcardstatus);
SDCardState SD_GetState(void);
SD_Error SD_ReadBlock(uint8_t *buf,long long addr,uint16_t blksize);  
SD_Error SD_ReadMultiBlocks(uint8_t *buf,long long  addr,uint16_t blksize,uint32_t nblks);  
SD_Error SD_WriteBlock(uint8_t *buf,long long addr,  uint16_t blksize);	
SD_Error SD_WriteMultiBlocks(uint8_t *buf,long long addr,uint16_t blksize,uint32_t nblks);
SD_Error SD_ProcessIRQSrc(void);

void SD_DMA_Config(uint32_t*mbuf,uint32_t bufsize,uint32_t dir);
//void SD_DMA_Config(uint32_t*mbuf,uint32_t bufsize,uint8_t dir); 

uint8_t SD_ReadDisk(uint8_t*buf,uint32_t sector,uint8_t cnt); 	//读sd卡,fatfs/usb调用
uint8_t SD_WriteDisk(uint8_t*buf,uint32_t sector,uint8_t cnt);	//写SD卡,fatfs/usb调用

#endif